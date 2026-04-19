/**
 * @file PressureSensor.cpp
 * @brief Implementation of PressureSensor and PressureSensorCalibrator.
 *
 * @author HardFOC Team
 * @date 2026
 * @copyright HardFOC
 */

#include "PressureSensor.h"
#include "esp_log.h"
#include "OsUtility.h"
static const char* TAG = "PressureSensor";

//=============================================================================
// PRESSURE SENSOR IMPLEMENTATION
//=============================================================================

PressureSensor::PressureSensor(PressureSensorConfig& cfg, 
                               const CalibrationConfig& cal_cfg) noexcept
    : config_(cfg)
    , cal_config_(cal_cfg)
    , calibrated_va_(cal_cfg.va_nominal)
    , zero_offset_voltage_(cal_cfg.va_nominal * 0.5f)  // Default to ideal midpoint
    , last_voltage_(0.0f)
    , last_pressure_(0.0f)
    , min_pressure_(1e9f)
    , max_pressure_(-1e9f)
    , sample_count_(0)
    , error_count_(0)
    , sensor_ok_(false)
    , swapped_(false)
    , zero_calibrated_(false)
    , swap_calibrated_(false)
{}

void PressureSensor::SetCalibratedVA(float va) noexcept {
    calibrated_va_ = va;
    // Update zero offset to new VA midpoint if not yet calibrated
    if (!zero_calibrated_) {
        zero_offset_voltage_ = va * 0.5f;
    }
}

bool PressureSensor::CalibrateZero(uint32_t num_samples, uint32_t sample_delay_ms) noexcept {
    if (num_samples == 0) {
        num_samples = cal_config_.calibration_samples;
    }
    
    auto& adc = FLUX_API.adc;
    
    float voltage_sum = 0.0f;
    uint32_t valid_samples = 0;
    
    for (uint32_t i = 0; i < num_samples; ++i) {
        float voltage = 0.0f;
        hf_adc_err_t err = adc.ReadVoltage(config_.adc_channel, voltage, 4);
        
        if (err == hf_adc_err_t::ADC_SUCCESS) {
            voltage_sum += voltage;
            ++valid_samples;
        }
        
        if (sample_delay_ms > 0) {
            os_delay_msec(sample_delay_ms);
        }
    }
    
    if (valid_samples < num_samples / 2) {
        ESP_LOGE(TAG, "%s: Zero calibration failed — insufficient samples (%lu/%lu)",
                 config_.label, 
                 static_cast<unsigned long>(valid_samples),
                 static_cast<unsigned long>(num_samples));
        return false;
    }
    
    zero_offset_voltage_ = voltage_sum / static_cast<float>(valid_samples);
    zero_calibrated_ = true;
    
    // Calculate how far off from ideal midpoint
    float ideal_midpoint = calibrated_va_ * 0.5f;
    float offset_error = zero_offset_voltage_ - ideal_midpoint;
    
    ESP_LOGI(TAG, "%s: Zero calibrated at %.4f V (ideal=%.4f V, offset=%.4f V)",
             config_.label,
             static_cast<double>(zero_offset_voltage_),
             static_cast<double>(ideal_midpoint),
             static_cast<double>(offset_error));
    
    return true;
}

float PressureSensor::GetEffectiveVoltageMin() const noexcept {
    if (config_.ratio_range == RatiometricRange::CUSTOM) {
        return config_.voltage_min;
    }
    return GetRatiometricVoltageMin(config_.ratio_range, calibrated_va_);
}

float PressureSensor::GetEffectiveVoltageMax() const noexcept {
    if (config_.ratio_range == RatiometricRange::CUSTOM) {
        return config_.voltage_max;
    }
    return GetRatiometricVoltageMax(config_.ratio_range, calibrated_va_);
}

float PressureSensor::GetExpectedAtmosphericVoltage() const noexcept {
    // Midpoint of the ratiometric range
    return (GetEffectiveVoltageMin() + GetEffectiveVoltageMax()) / 2.0f;
}

bool PressureSensor::Sample(uint8_t avg_samples) noexcept {
    auto& adc = FLUX_API.adc;

    float voltage = 0.0f;
    hf_adc_err_t err = adc.ReadVoltage(config_.adc_channel, voltage, avg_samples);

    if (err != hf_adc_err_t::ADC_SUCCESS) {
        ++error_count_;
        sensor_ok_ = false;
        return false;
    }

    Update(voltage);
    return true;
}

void PressureSensor::ConfigureFilter(float cutoff_hz, float sample_hz) noexcept {
    if (cutoff_hz <= 0.0f || sample_hz <= 0.0f || cutoff_hz >= 0.5f * sample_hz) {
        // Disable filter (or unrepresentable cutoff).
        filter_alpha_ = 1.0f;
    } else {
        // First-order RC equivalent: alpha = 1 - exp(-2*pi*fc/fs)
        const float kTwoPi = 6.28318530717958647692f;
        filter_alpha_ = 1.0f - std::exp(-kTwoPi * cutoff_hz / sample_hz);
    }
    filter_primed_ = false;
}

void PressureSensor::ResetFilter() noexcept {
    filter_primed_ = false;
}

void PressureSensor::Update(float voltage_raw) noexcept {
    // ── Voltage IIR ────────────────────────────────────────────────────────
    if (!filter_primed_) {
        voltage_filt_state_ = voltage_raw;
        filter_primed_ = true;
    } else {
        voltage_filt_state_ += filter_alpha_ * (voltage_raw - voltage_filt_state_);
    }

    // ── Voltage → pressure on the filtered voltage ─────────────────────────
    const float raw_pressure = VoltageToPressureRaw(voltage_filt_state_);
    const float final_pressure = swapped_ ? -raw_pressure : raw_pressure;
    pressure_filt_state_ = final_pressure;

    // ── Bookkeeping ────────────────────────────────────────────────────────
    last_voltage_ = voltage_filt_state_;
    last_pressure_ = final_pressure;
    ++sample_count_;
    sensor_ok_ = true;
    if (final_pressure < min_pressure_) min_pressure_ = final_pressure;
    if (final_pressure > max_pressure_) max_pressure_ = final_pressure;

    // ── Atomic publish for lock-free control-loop consumers ────────────────
    voltage_filt_atomic_.store(voltage_filt_state_, std::memory_order_release);
    pressure_filt_atomic_.store(final_pressure, std::memory_order_release);
}

SwapCalibrationResult PressureSensor::CalibrateSwap(
    uint32_t duration_ms,
    uint32_t sample_interval_ms,
    PressureDirection expected_direction,
    bool apply_if_detected) noexcept 
{
    SwapCalibrationResult result;
    auto& adc = FLUX_API.adc;
    
    // Determine ground truth direction
    PressureDirection ground_truth;
    if (expected_direction != PressureDirection::DIR_EITHER) {
        // User explicitly provided expected direction
        ground_truth = expected_direction;
    } else if (config_.expected_dir != PressureDirection::DIR_EITHER) {
        // Use configured direction as ground truth for single-ended sensors
        ground_truth = config_.expected_dir;
    } else {
        // Differential sensor with no expected direction provided
        result.cant_determine = true;
        result.success = true;  // Not a failure, just can't determine
        result.message = "DIR_EITHER config requires expected_direction parameter";
        ESP_LOGW(TAG, "%s: %s", config_.label, result.message);
        swap_calibrated_ = true;
        return result;
    }
    
    result.ground_truth_direction = ground_truth;
    
    ESP_LOGI(TAG, "%s: Starting swap calibration (duration=%lu ms, ground_truth=%s)",
             config_.label,
             static_cast<unsigned long>(duration_ms),
             DirectionToString(ground_truth));
    
    // Sample for the specified duration, tracking min/max raw pressure
    float min_raw = 1e9f;
    float max_raw = -1e9f;
    uint32_t samples_taken = 0;
    uint32_t elapsed_ms = 0;
    
    while (elapsed_ms < duration_ms) {
        float voltage = 0.0f;
        hf_adc_err_t err = adc.ReadVoltage(config_.adc_channel, voltage, 8);
        
        if (err == hf_adc_err_t::ADC_SUCCESS) {
            float raw_pressure = VoltageToPressureRaw(voltage);
            if (raw_pressure < min_raw) min_raw = raw_pressure;
            if (raw_pressure > max_raw) max_raw = raw_pressure;
            ++samples_taken;
        }
        
        os_delay_msec(sample_interval_ms);
        elapsed_ms += sample_interval_ms;
    }
    
    result.sample_count = samples_taken;
    result.min_pressure_raw = min_raw;
    result.max_pressure_raw = max_raw;
    result.pressure_range = max_raw - min_raw;
    
    if (samples_taken == 0) {
        result.success = false;
        result.message = "No valid samples during calibration";
        ESP_LOGE(TAG, "%s: %s", config_.label, result.message);
        return result;
    }
    
    ESP_LOGI(TAG, "%s: Calibration samples=%lu, min=%.4f, max=%.4f, range=%.4f",
             config_.label,
             static_cast<unsigned long>(samples_taken),
             static_cast<double>(min_raw),
             static_cast<double>(max_raw),
             static_cast<double>(result.pressure_range));
    
    // Check if we saw enough pressure range to make a determination
    float full_scale = std::abs(config_.pressure_max - config_.pressure_min);
    float min_range_required = cal_config_.swap_min_range_threshold * full_scale;
    
    if (result.pressure_range < min_range_required) {
        result.cant_determine = true;
        result.success = true;
        result.message = "Insufficient pressure change to determine direction";
        ESP_LOGW(TAG, "%s: %s (range=%.4f, needed=%.4f)",
                 config_.label, result.message,
                 static_cast<double>(result.pressure_range),
                 static_cast<double>(min_range_required));
        swap_calibrated_ = true;
        return result;
    }
    
    // Determine which direction the pressure predominantly went
    // Use threshold relative to full scale to determine "definitely positive" or "definitely negative"
    float direction_threshold = cal_config_.swap_direction_threshold * full_scale;
    
    bool went_positive = (max_raw > direction_threshold) && 
                         (std::abs(max_raw) > std::abs(min_raw));
    bool went_negative = (min_raw < -direction_threshold) && 
                         (std::abs(min_raw) > std::abs(max_raw));
    
    if (went_positive) {
        result.measured_direction = PressureDirection::DIR_POSITIVE;
    } else if (went_negative) {
        result.measured_direction = PressureDirection::DIR_NEGATIVE;
    } else {
        // Both directions or neither — can't determine clearly
        result.measured_direction = PressureDirection::DIR_EITHER;
        result.cant_determine = true;
        result.success = true;
        result.message = "Pressure went both directions or was ambiguous";
        ESP_LOGW(TAG, "%s: %s (max=%.4f, min=%.4f)",
                 config_.label, result.message,
                 static_cast<double>(max_raw),
                 static_cast<double>(min_raw));
        swap_calibrated_ = true;
        return result;
    }
    
    ESP_LOGI(TAG, "%s: Measured direction = %s",
             config_.label, DirectionToString(result.measured_direction));
    
    // Compare measured direction to ground truth
    bool is_opposite = (result.measured_direction == PressureDirection::DIR_POSITIVE && 
                        ground_truth == PressureDirection::DIR_NEGATIVE) ||
                       (result.measured_direction == PressureDirection::DIR_NEGATIVE && 
                        ground_truth == PressureDirection::DIR_POSITIVE);
    
    result.swap_detected = is_opposite;
    
    if (is_opposite) {
        // Swap detected!
        if (config_.auto_swap && apply_if_detected) {
            swapped_ = true;
            result.swap_applied = true;
            result.message = "Swap detected and applied";
            ESP_LOGW(TAG, "%s: SWAP DETECTED AND APPLIED (measured=%s, expected=%s)",
                     config_.label,
                     DirectionToString(result.measured_direction),
                     DirectionToString(ground_truth));
        } else {
            result.swap_applied = false;
            result.message = "Swap detected but NOT applied (auto_swap=false)";
            ESP_LOGW(TAG, "%s: SWAP DETECTED but NOT applied (auto_swap=%s, apply=%s)",
                     config_.label,
                     config_.auto_swap ? "true" : "false",
                     apply_if_detected ? "true" : "false");
        }
    } else {
        result.swap_applied = false;
        result.message = "Direction matches expected — no swap needed";
        ESP_LOGI(TAG, "%s: Direction OK (measured=%s, expected=%s)",
                 config_.label,
                 DirectionToString(result.measured_direction),
                 DirectionToString(ground_truth));
    }
    
    result.success = true;
    swap_calibrated_ = true;
    return result;
}

void PressureSensor::SetSwapped(bool swapped) noexcept {
    swapped_ = swapped;
    // Recalculate last pressure with new swap state
    if (sample_count_ > 0) {
        float raw = VoltageToPressureRaw(last_voltage_);
        last_pressure_ = swapped_ ? -raw : raw;
    }
    ESP_LOGI(TAG, "%s: Swap %s",
             config_.label, swapped_ ? "ENABLED" : "DISABLED");
}

void PressureSensor::ToggleSwap() noexcept {
    SetSwapped(!swapped_);
}

void PressureSensor::ResetStatistics() noexcept {
    min_pressure_ = 1e9f;
    max_pressure_ = -1e9f;
    sample_count_ = 0;
    error_count_  = 0;
}

float PressureSensor::VoltageToPressureRaw(float voltage) const noexcept {
    const float v_min = GetEffectiveVoltageMin();
    const float v_max = GetEffectiveVoltageMax();
    const float v_span = v_max - v_min;

    if (std::abs(v_span) < 1e-6f) return 0.0f;

    // Sensitivity from the configured ratiometric span (PSI per Volt).
    // Sign is naturally negative for vacuum (DIR_NEGATIVE) sensors where
    // pressure_max < pressure_min.
    const float sensitivity = (config_.pressure_max - config_.pressure_min) / v_span;

    if (zero_calibrated_) {
        // The observed atmospheric voltage IS the "atmospheric pressure" anchor.
        // For DIR_POSITIVE / DIR_NEGATIVE this is pressure_min (= 0 PSI gauge);
        // for DIR_EITHER (differential) it is 0 differential pressure, which
        // also corresponds to pressure_min + range/2. In both conventions the
        // delta from atmospheric voltage to current voltage maps linearly to
        // the delta from atmospheric pressure.
        const float atm_pressure =
            (config_.expected_dir == PressureDirection::DIR_EITHER)
                ? (config_.pressure_min + config_.pressure_max) * 0.5f  // 0 for symmetric ranges
                : config_.pressure_min;                                  // 0 PSI gauge for single-ended
        return atm_pressure + (voltage - zero_offset_voltage_) * sensitivity;
    }

    // No zero calibration yet — fall back to nominal ratiometric mapping.
    const float normalized = (voltage - v_min) / v_span;
    return config_.pressure_min + normalized * (config_.pressure_max - config_.pressure_min);
}

//=============================================================================
// PRESSURE SENSOR CALIBRATOR IMPLEMENTATION
//=============================================================================

PressureSensorCalibrator::PressureSensorCalibrator(
    PressureSensor** sensors, 
    size_t num_sensors,
    const CalibrationConfig& cal_cfg) noexcept
    : sensors_(sensors)
    , num_sensors_(num_sensors)
    , cal_config_(cal_cfg)
    , calibrated_va_(cal_cfg.va_nominal)
    , va_calibrated_(false)
    , last_error_(nullptr)
{}

float PressureSensorCalibrator::CalibrateVA() noexcept {
    ESP_LOGW(TAG, "=== VA Supply Calibration === ENTER");
    
    // Count and identify sensors used for VA calibration
    size_t cal_sensor_count = 0;
    for (size_t i = 0; i < num_sensors_; ++i) {
        if (sensors_[i]->GetConfig().use_for_va_cal) {
            ++cal_sensor_count;
        }
    }
    
    if (cal_sensor_count == 0) {
        last_error_ = "No sensors marked for VA calibration (use_for_va_cal=true)";
        ESP_LOGE(TAG, "%s", last_error_);
        return 0.0f;
    }
    
    ESP_LOGI(TAG, "Using %zu sensors for VA calibration", cal_sensor_count);
    
    auto& adc = FLUX_API.adc;
    
    // Storage for each calibration sensor
    constexpr size_t MAX_CAL_SENSORS = 16;
    float voltage_sums[MAX_CAL_SENSORS] = {};
    uint32_t valid_samples[MAX_CAL_SENSORS] = {};
    size_t cal_indices[MAX_CAL_SENSORS] = {};
    size_t cal_count = 0;
    
    // Build calibration sensor list
    for (size_t i = 0; i < num_sensors_ && cal_count < MAX_CAL_SENSORS; ++i) {
        if (sensors_[i]->GetConfig().use_for_va_cal) {
            cal_indices[cal_count++] = i;
        }
    }
    
    ESP_LOGW(TAG, "VA cal: sampling %lu rounds x %zu sensors (4 avg each)...",
             static_cast<unsigned long>(cal_config_.calibration_samples), cal_count);
    // Sample each calibration sensor multiple times
    for (uint32_t sample = 0; sample < cal_config_.calibration_samples; ++sample) {
        for (size_t ci = 0; ci < cal_count; ++ci) {
            size_t idx = cal_indices[ci];
            const auto& cfg = sensors_[idx]->GetConfig();

            float voltage = 0.0f;
            hf_adc_err_t err = adc.ReadVoltage(cfg.adc_channel, voltage, 4);

            if (err == hf_adc_err_t::ADC_SUCCESS) {
                voltage_sums[ci] += voltage;
                ++valid_samples[ci];
            } else {
                ESP_LOGW(TAG, "VA cal: ReadVoltage failed (sample=%lu, ch=%s, err=%d)",
                         static_cast<unsigned long>(sample), cfg.label,
                         static_cast<int>(err));
            }
        }
        os_delay_msec(cal_config_.calibration_sample_delay_ms);

        // Heartbeat every ~16 rounds (~80 ms with default 5 ms delay)
        if ((sample & 0x0F) == 0) {
            ESP_LOGW(TAG, "VA cal progress: round %lu/%lu",
                     static_cast<unsigned long>(sample),
                     static_cast<unsigned long>(cal_config_.calibration_samples));
        }
    }
    ESP_LOGW(TAG, "VA cal: sampling done — averaging");
    
    // Calculate averages and verify all sensors responded
    float avg_voltages[MAX_CAL_SENSORS] = {};
    for (size_t ci = 0; ci < cal_count; ++ci) {
        size_t idx = cal_indices[ci];
        const auto& cfg = sensors_[idx]->GetConfig();
        
        if (valid_samples[ci] == 0) {
            last_error_ = "Calibration sensor offline — no valid samples";
            ESP_LOGE(TAG, "  %s: No valid samples — sensor offline?", cfg.label);
            return 0.0f;
        }
        
        avg_voltages[ci] = voltage_sums[ci] / static_cast<float>(valid_samples[ci]);
        ESP_LOGI(TAG, "  %s: avg=%.4f V  (n=%lu)",
                 cfg.label,
                 static_cast<double>(avg_voltages[ci]),
                 static_cast<unsigned long>(valid_samples[ci]));
    }
    
    // Cross-channel verification: all sensors should read similar voltages
    float v_min = avg_voltages[0];
    float v_max = avg_voltages[0];
    for (size_t ci = 1; ci < cal_count; ++ci) {
        if (avg_voltages[ci] < v_min) v_min = avg_voltages[ci];
        if (avg_voltages[ci] > v_max) v_max = avg_voltages[ci];
    }
    
    float deviation = v_max - v_min;
    ESP_LOGI(TAG, "Cross-channel deviation: %.4f V (tolerance: %.3f V)",
             static_cast<double>(deviation),
             static_cast<double>(cal_config_.atmospheric_voltage_tolerance));
    
    if (deviation > cal_config_.atmospheric_voltage_tolerance) {
        last_error_ = "Sensors disagree — check wiring or pressure lines";
        ESP_LOGE(TAG, "CALIBRATION FAILED: Sensors disagree by %.3f V",
                 static_cast<double>(deviation));
        ESP_LOGE(TAG, "  Check that all calibration sensors are at atmospheric");
        return 0.0f;
    }
    
    // Calculate VA from average of all atmospheric readings
    // At atmospheric: sensor output = VA/2 (midpoint of ratiometric range)
    float sum_voltage = 0.0f;
    for (size_t ci = 0; ci < cal_count; ++ci) {
        sum_voltage += avg_voltages[ci];
    }
    float avg_atmospheric_voltage = sum_voltage / static_cast<float>(cal_count);
    
    // VA = 2 × atmospheric voltage
    float calculated_va = 2.0f * avg_atmospheric_voltage;
    
    ESP_LOGI(TAG, "Atmospheric average: %.4f V → Calculated VA: %.3f V",
             static_cast<double>(avg_atmospheric_voltage),
             static_cast<double>(calculated_va));
    
    // Validate VA is within acceptable range
    if (calculated_va < cal_config_.va_min_valid) {
        last_error_ = "VA below minimum — check power supply";
        ESP_LOGE(TAG, "CALIBRATION FAILED: VA=%.2f V below minimum %.2f V",
                 static_cast<double>(calculated_va),
                 static_cast<double>(cal_config_.va_min_valid));
        return 0.0f;
    }
    
    if (calculated_va > cal_config_.va_max_valid) {
        last_error_ = "VA above maximum — overvoltage condition";
        ESP_LOGE(TAG, "CALIBRATION FAILED: VA=%.2f V exceeds maximum %.2f V",
                 static_cast<double>(calculated_va),
                 static_cast<double>(cal_config_.va_max_valid));
        return 0.0f;
    }
    
    // Success
    calibrated_va_ = calculated_va;
    va_calibrated_ = true;
    
    float v_5pct = 0.05f * calibrated_va_;
    float v_95pct = 0.95f * calibrated_va_;
    
    ESP_LOGI(TAG, "=== VA CALIBRATION SUCCESSFUL ===");
    ESP_LOGI(TAG, "  Calibrated VA: %.3f V", static_cast<double>(calibrated_va_));
    ESP_LOGI(TAG, "  Sensor range:  %.3f V (5%%) to %.3f V (95%%)",
             static_cast<double>(v_5pct), static_cast<double>(v_95pct));
    
    return calibrated_va_;
}

void PressureSensorCalibrator::ApplyVA(float va) noexcept {
    calibrated_va_ = va;
    for (size_t i = 0; i < num_sensors_; ++i) {
        sensors_[i]->SetCalibratedVA(va);
    }
    ESP_LOGI(TAG, "Applied VA=%.3f V to %zu sensors", 
             static_cast<double>(va), num_sensors_);
}

bool PressureSensorCalibrator::CalibrateAllZeros() noexcept {
    ESP_LOGI(TAG, "=== Zero Offset Calibration ===");
    
    bool all_ok = true;
    for (size_t i = 0; i < num_sensors_; ++i) {
        if (!sensors_[i]->CalibrateZero()) {
            all_ok = false;
        }
    }
    
    if (all_ok) {
        ESP_LOGI(TAG, "=== ZERO CALIBRATION COMPLETE ===");
    } else {
        last_error_ = "One or more sensors failed zero calibration";
        ESP_LOGW(TAG, "Some sensors failed zero calibration");
    }
    
    return all_ok;
}

size_t PressureSensorCalibrator::CalibrateAllSwaps(
    uint32_t duration_ms,
    uint32_t sample_interval_ms,
    bool apply_if_detected) noexcept 
{
    ESP_LOGI(TAG, "=== Swap Detection Calibration ===");
    ESP_LOGI(TAG, "Duration: %lu ms per sensor, %zu sensors total",
             static_cast<unsigned long>(duration_ms), num_sensors_);
    ESP_LOGI(TAG, "NOTE: Actuate valves now to create pressure changes!");
    
    size_t success_count = 0;
    size_t swap_detected_count = 0;
    size_t swap_applied_count = 0;
    size_t cant_determine_count = 0;
    
    for (size_t i = 0; i < num_sensors_; ++i) {
        SwapCalibrationResult result = sensors_[i]->CalibrateSwap(
            duration_ms,
            sample_interval_ms,
            PressureDirection::DIR_EITHER,  // Use configured direction as ground truth
            apply_if_detected);
        
        if (result.success) {
            ++success_count;
            if (result.swap_detected) {
                ++swap_detected_count;
                if (result.swap_applied) {
                    ++swap_applied_count;
                }
            }
            if (result.cant_determine) {
                ++cant_determine_count;
            }
        }
    }
    
    ESP_LOGI(TAG, "=== SWAP CALIBRATION COMPLETE ===");
    ESP_LOGI(TAG, "  Success: %zu/%zu sensors", success_count, num_sensors_);
    ESP_LOGI(TAG, "  Swaps detected: %zu, applied: %zu", 
             swap_detected_count, swap_applied_count);
    ESP_LOGI(TAG, "  Could not determine: %zu", cant_determine_count);
    
    if (cant_determine_count > 0) {
        last_error_ = "Some sensors couldn't determine swap (insufficient pressure change)";
    }
    
    return success_count;
}
