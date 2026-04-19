#include "PressureSensorThread.h"

#include "OsUtility.h"
#include "esp_log.h"
#include "freertos/task.h"

#include <cstring>

static const char* TAG_PRESSURE = "Pressure";

PressureSensorThread::PressureSensorThread(PressureSensorConfig* sensor_configs,
                                           std::size_t num_sensors,
                                           const CalibrationConfig& cal_config,
                                           SharedState shared_state,
                                           uint32_t sample_interval_ms,
                                           uint8_t avg_samples,
                                           float filter_cutoff_hz) noexcept
    : BaseThread("PressureThread"),
      sensor_configs_(sensor_configs),
      num_sensors_(num_sensors),
      cal_config_(cal_config),
      shared_state_(shared_state),
      sample_interval_ms_(sample_interval_ms),
      avg_samples_(avg_samples),
      filter_cutoff_hz_(filter_cutoff_hz),
      sensors_{},
      sensor_ptrs_{},
      calibrator_(nullptr),
      iteration_(0),
      thread_stack_{} {}

bool PressureSensorThread::Initialize() noexcept {
    return CreateBaseThread(thread_stack_,
                            sizeof(thread_stack_),
                            kThreadPriority,
                            kThreadPriority,
                            0,
                            OS_AUTO_START);
}

void PressureSensorThread::SetFilterCutoff(float cutoff_hz) noexcept {
    filter_cutoff_hz_ = cutoff_hz;
    const float fs = (sample_interval_ms_ == 0)
                         ? 1000.0f
                         : 1000.0f / static_cast<float>(sample_interval_ms_);
    for (std::size_t i = 0; i < num_sensors_; ++i) {
        if (sensors_[i]) sensors_[i]->ConfigureFilter(filter_cutoff_hz_, fs);
    }
}

bool PressureSensorThread::Setup() noexcept {
    // WARN so this line survives aggressive log filtering during bring-up
    ESP_LOGW(TAG_PRESSURE, "Setup() entered on worker task");
    taskYIELD();

    if (!sensor_configs_ || num_sensors_ == 0 || num_sensors_ > kMaxSensors) {
        ESP_LOGE(TAG_PRESSURE, "Invalid sensor configuration (count=%u)",
                 static_cast<unsigned>(num_sensors_));
        return false;
    }

    ESP_LOGI(TAG_PRESSURE, "Pressure thread setup");

    for (std::size_t i = 0; i < num_sensors_; ++i) {
        sensors_[i] = std::make_unique<PressureSensor>(sensor_configs_[i], cal_config_);
        if (!sensors_[i]) {
            ESP_LOGE(TAG_PRESSURE, "Failed to create PressureSensor CH%u", static_cast<unsigned>(i));
            return false;
        }
        sensor_ptrs_[i] = sensors_[i].get();
        shared_state_.sensor_slots[i] = sensors_[i].get();
    }

    calibrator_ = std::make_unique<PressureSensorCalibrator>(sensor_ptrs_.data(), num_sensors_, cal_config_);
    if (!calibrator_) {
        ESP_LOGE(TAG_PRESSURE, "Failed to create PressureSensorCalibrator");
        return false;
    }

    ESP_LOGI(TAG_PRESSURE, "Pressure sensor calibration start (2s settle)...");
    os_delay_msec(2000);
    ESP_LOGW(TAG_PRESSURE, "Settle complete — starting VA calibration");

    float va = calibrator_->CalibrateVA();
    if (va == 0.0f) {
        ESP_LOGE(TAG_PRESSURE, "VA calibration failed: %s", calibrator_->GetLastError());
        va = cal_config_.va_nominal;
        ESP_LOGW(TAG_PRESSURE, "Using nominal VA=%.2f V", static_cast<double>(va));
    }

    shared_state_.calibrated_va = va;
    calibrator_->ApplyVA(va);

    if (!calibrator_->CalibrateAllZeros()) {
        ESP_LOGW(TAG_PRESSURE, "Some sensors failed zero calibration");
    }

    // ── Configure per-channel low-pass IIR ─────────────────────────────────
    const float fs_hz = (sample_interval_ms_ == 0)
                            ? 1000.0f
                            : 1000.0f / static_cast<float>(sample_interval_ms_);
    for (std::size_t i = 0; i < num_sensors_; ++i) {
        sensors_[i]->ConfigureFilter(filter_cutoff_hz_, fs_hz);
    }
    ESP_LOGI(TAG_PRESSURE,
             "Filter: 1-pole IIR, cutoff=%.1f Hz, sample=%.0f Hz",
             static_cast<double>(filter_cutoff_hz_),
             static_cast<double>(fs_hz));

    // ── Resolve the ADS7952 handler for batched live sampling ──────────────
    auto& adc = FLUX_API.adc;
    BaseAdc* drv = adc.Get(sensor_configs_[0].adc_channel);
    if (drv) {
        // All sensor channels in this app live on a single ADS7952 device,
        // so the BaseAdc we got is in fact the Ads7952Handler the manager
        // registered for those channels. (See AdcManager::InitializeAds7952().)
        ads7952_handler_ = static_cast<Ads7952Handler*>(drv);
    } else {
        ESP_LOGE(TAG_PRESSURE, "Unable to resolve ADS7952 handler — falling back to per-channel reads");
    }

    // Build the hw-channel array used by ReadMultipleChannels(); requires
    // physical channel ids on the ADS7952. We recover them from the
    // functional → physical mapping table once per setup.
    for (std::size_t i = 0; i < num_sensors_; ++i) {
        const auto* mapping = GetAdcMapping(sensor_configs_[i].adc_channel);
        hw_channels_[i] = mapping ? static_cast<hf_channel_id_t>(mapping->physical_channel)
                                  : static_cast<hf_channel_id_t>(i);
    }

    // ── Prime the filtered atomics so consumers see plausible data ─────────
    for (std::size_t i = 0; i < num_sensors_; ++i) {
        for (int j = 0; j < 4; ++j) {
            sensors_[i]->Sample(avg_samples_);
            os_delay_msec(2);
        }
    }

    ESP_LOGI(TAG_PRESSURE,
             "Calibration complete: VA=%.3f V, sample period=%lu ms",
             static_cast<double>(shared_state_.calibrated_va),
             static_cast<unsigned long>(sample_interval_ms_));

    shared_state_.calibration_complete = true;
    shared_state_.thread_ready.store(true, std::memory_order_release);
    return true;
}

uint32_t PressureSensorThread::Step() noexcept {
    if (!shared_state_.system_running.load(std::memory_order_acquire)) {
        Stop();
        return 1;
    }

    // ── Acquire all channels in one SPI transaction (Auto-1) ───────────────
    bool batched_ok = false;
    if (ads7952_handler_) {
        const hf_adc_err_t err = ads7952_handler_->ReadMultipleChannels(
            hw_channels_.data(),
            static_cast<hf_u8_t>(num_sensors_),
            batch_counts_.data(),
            batch_voltages_.data());
        batched_ok = (err == hf_adc_err_t::ADC_SUCCESS);
        if (batched_ok) {
            for (std::size_t i = 0; i < num_sensors_; ++i) {
                sensors_[i]->Update(batch_voltages_[i]);
            }
        }
    }

    if (!batched_ok) {
        // Fallback: per-channel reads through the AdcManager.
        for (std::size_t i = 0; i < num_sensors_; ++i) {
            sensors_[i]->Sample(1);
        }
    }

    // ── Throttled status print (~5 Hz at 1 ms sample period) ──────────────
    const uint32_t fs_hz = (sample_interval_ms_ == 0)
                               ? 1000U
                               : 1000U / sample_interval_ms_;
    const uint32_t print_period = (fs_hz > 0) ? (fs_hz / 5U) : 1U;  // ~5 Hz
    const uint32_t effective_period = (print_period == 0U) ? 1U : print_period;

    if ((++iteration_ % effective_period) == 0) {
        ESP_LOGI(TAG_PRESSURE, "─── Pressure Readings ─── (filtered)");
        for (std::size_t i = 0; i < num_sensors_; ++i) {
            const auto& sensor = *sensors_[i];
            const auto& cfg = sensor.GetConfig();
            if (sensor.IsOk()) {
                ESP_LOGI(TAG_PRESSURE,
                         "  %-10s: %+9.4f %-4s  (V=%6.4f)%s",
                         cfg.label,
                         static_cast<double>(sensor.GetPressureFiltered()),
                         cfg.units,
                         static_cast<double>(sensor.GetVoltageFiltered()),
                         sensor.IsSwapped() ? " [SW]" : "");
            } else {
                ESP_LOGW(TAG_PRESSURE,
                         "  %-10s: ERROR (count=%lu)",
                         cfg.label,
                         static_cast<unsigned long>(sensor.GetErrorCount()));
            }
        }
    }

    return sample_interval_ms_;
}

bool PressureSensorThread::Cleanup() noexcept {
    for (std::size_t i = 0; i < num_sensors_; ++i) {
        shared_state_.sensor_slots[i] = nullptr;
    }

    calibrator_.reset();
    for (std::size_t i = 0; i < num_sensors_; ++i) {
        sensors_[i].reset();
        sensor_ptrs_[i] = nullptr;
    }
    ads7952_handler_ = nullptr;

    shared_state_.thread_ready.store(false, std::memory_order_release);
    return true;
}

bool PressureSensorThread::ResetVariables() noexcept {
    iteration_ = 0;
    shared_state_.calibration_complete = false;
    return true;
}
