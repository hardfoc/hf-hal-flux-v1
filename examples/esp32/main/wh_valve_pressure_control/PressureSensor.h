/**
 * @file PressureSensor.h
 * @brief Ratiometric pressure sensor class with VA and zero calibration.
 *
 * Reads raw ADC voltage and converts to engineering pressure units.
 * Supports:
 *   - Ratiometric sensors (voltage scales with VA)
 *   - Differential and single-ended configurations
 *   - Auto line-swap detection for single-ended sensors
 *   - VA calibration from atmospheric readings
 *   - Per-sensor zero offset calibration
 *
 * @author HardFOC Team
 * @date 2026
 * @copyright HardFOC
 */

#pragma once

#include "PressureSensorConfig.h"
#include "api/Flux.h"
#include <cmath>
#include <cstdint>

/**
 * @brief Reads raw ADC voltage and converts to engineering pressure units.
 *
 * Thread-safe: AdcManager reads are mutex-protected internally, and the
 * ADS7952 is on its own SPI bus so no contention with other SPI traffic.
 *
 * Supports ratiometric sensors — voltage_min/max are computed from actual VA.
 *
 * Line swap handling (calibration-time only via CalibrateSwap()):
 *   - For single-ended (DIR_POSITIVE/DIR_NEGATIVE): auto-detect using configured
 *     direction as ground truth, or override with expected_direction parameter
 *   - For differential (DIR_EITHER): REQUIRES expected_direction parameter to
 *     determine ground truth — otherwise can't auto-detect
 *   - If swap detected and auto_swap=true: swap applied automatically
 *   - If swap detected and auto_swap=false: reported but not applied
 *
 * Calibration sequence:
 *   1. CalibrateVA() — determine actual supply voltage from atmospheric sensors
 *   2. CalibrateZero() — for each sensor, store atmospheric offset
 *   3. CalibrateSwap() — sample for a duration while valve opens, detect swap
 *   4. Sample() — normal operation (just applies swap flag, no detection)
 */
class PressureSensor {
public:
    /**
     * @brief Construct a pressure sensor from configuration.
     * @param cfg Reference to sensor configuration (must remain valid)
     * @param cal_cfg Reference to calibration configuration
     */
    explicit PressureSensor(PressureSensorConfig& cfg, 
                           const CalibrationConfig& cal_cfg) noexcept;

    //=========================================================================
    // CALIBRATION
    //=========================================================================

    /**
     * @brief Update the VA reference for ratiometric voltage calculations.
     * @param va Calibrated VA voltage in volts
     */
    void SetCalibratedVA(float va) noexcept;

    /**
     * @brief Calibrate the zero offset at atmospheric pressure.
     *
     * Samples the sensor multiple times and stores the average reading
     * as the zero offset. Call this when sensor is at atmospheric/zero
     * differential pressure.
     *
     * @param num_samples Number of samples to average (default from config)
     * @param sample_delay_ms Delay between samples in ms
     * @return true if calibration succeeded
     */
    bool CalibrateZero(uint32_t num_samples = 0, uint32_t sample_delay_ms = 5) noexcept;

    /**
     * @brief Calibrate swap by sampling during pressure application.
     *
     * Samples for specified duration to track min/max pressure readings.
     * Another thread should actuate valves to create measurable pressure change
     * during this time. After sampling:
     *   - Determines which direction pressure predominantly went
     *   - Compares to ground truth (expected_direction or configured direction)
     *   - If opposite: swap is detected
     *
     * For single-ended sensors (DIR_POSITIVE/DIR_NEGATIVE):
     *   - Ground truth = expected_direction if provided, else configured direction
     *   - Can auto-detect swap based on measured vs ground truth
     *
     * For differential sensors (DIR_EITHER configured):
     *   - Ground truth = expected_direction (REQUIRED)
     *   - If not provided: cant_determine=true, no action taken
     *
     * @param duration_ms How long to sample for (during which valves may actuate)
     * @param sample_interval_ms Delay between samples
     * @param expected_direction Override ground truth direction (required for DIR_EITHER)
     * @param apply_if_detected If true and swap detected, apply it automatically
     * @return SwapCalibrationResult with details of what was detected/applied
     */
    SwapCalibrationResult CalibrateSwap(
        uint32_t duration_ms,
        uint32_t sample_interval_ms = 10,
        PressureDirection expected_direction = PressureDirection::DIR_EITHER,
        bool apply_if_detected = true) noexcept;

    /**
     * @brief Get the atmospheric zero offset voltage.
     * @return Zero offset in volts (raw voltage at 0 pressure)
     */
    float GetZeroOffset() const noexcept { return zero_offset_voltage_; }

    /**
     * @brief Check if zero calibration has been performed.
     */
    bool IsZeroCalibrated() const noexcept { return zero_calibrated_; }

    /**
     * @brief Check if swap calibration has been performed.
     */
    bool IsSwapCalibrated() const noexcept { return swap_calibrated_; }

    //=========================================================================
    // VOLTAGE RANGE
    //=========================================================================

    /**
     * @brief Get effective voltage_min accounting for ratiometric scaling.
     */
    float GetEffectiveVoltageMin() const noexcept;

    /**
     * @brief Get effective voltage_max accounting for ratiometric scaling.
     */
    float GetEffectiveVoltageMax() const noexcept;

    /**
     * @brief Get the expected voltage at atmospheric (midpoint).
     */
    float GetExpectedAtmosphericVoltage() const noexcept;

    //=========================================================================
    // SAMPLING
    //=========================================================================

    /**
     * @brief Sample the sensor and update internal state.
     *
     * Simply reads voltage, converts to pressure, and applies swap if enabled.
     * No swap detection is performed here — use CalibrateSwap() during
     * calibration phase to determine and set swap state.
     *
     * @param avg_samples Number of ADC samples to average (default 8)
     * @return true if read succeeded
     */
    bool Sample(uint8_t avg_samples = 8) noexcept;

    //=========================================================================
    // LINE SWAP CONTROL
    //=========================================================================

    /**
     * @brief Manually set swap state.
     *
     * For DIR_EITHER sensors, this is the only way to handle line swaps.
     * Can also be used to override auto-swap behavior.
     *
     * @param swapped true to invert pressure direction
     */
    void SetSwapped(bool swapped) noexcept;

    /**
     * @brief Toggle swap state.
     */
    void ToggleSwap() noexcept;

    //=========================================================================
    // ACCESSORS
    //=========================================================================

    float GetPressure()    const noexcept { return last_pressure_; }
    float GetVoltage()     const noexcept { return last_voltage_; }
    float GetMinPressure() const noexcept { return min_pressure_; }
    float GetMaxPressure() const noexcept { return max_pressure_; }
    float GetCalibratedVA() const noexcept { return calibrated_va_; }
    bool  IsOk()           const noexcept { return sensor_ok_; }
    bool  IsSwapped()      const noexcept { return swapped_; }
    PressureDirection GetConfiguredDirection() const noexcept { return config_.expected_dir; }
    uint32_t GetSampleCount() const noexcept { return sample_count_; }
    uint32_t GetErrorCount()  const noexcept { return error_count_; }
    const PressureSensorConfig& GetConfig() const noexcept { return config_; }

    /**
     * @brief Reset min/max/count statistics.
     */
    void ResetStatistics() noexcept;

private:
    /**
     * @brief Convert voltage to pressure WITHOUT applying swap.
     */
    float VoltageToPressureRaw(float voltage) const noexcept;

    PressureSensorConfig& config_;
    const CalibrationConfig& cal_config_;
    
    float calibrated_va_;
    float zero_offset_voltage_;     ///< Voltage at atmospheric (ideal = VA/2)
    float last_voltage_;
    float last_pressure_;
    float min_pressure_;
    float max_pressure_;
    uint32_t sample_count_;
    uint32_t error_count_;
    bool sensor_ok_;
    bool swapped_;                  ///< True if pressure direction is inverted
    bool zero_calibrated_;          ///< True if CalibrateZero() has been called
    bool swap_calibrated_;          ///< True if CalibrateSwap() has been called
};

//=============================================================================
// CALIBRATION HELPER CLASS
//=============================================================================

/**
 * @brief Manages multi-sensor VA calibration.
 *
 * Uses sensors marked with use_for_va_cal=true to determine actual VA.
 * Also performs per-sensor zero offset calibration.
 */
class PressureSensorCalibrator {
public:
    /**
     * @brief Construct calibrator with sensor array.
     * @param sensors Array of PressureSensor pointers
     * @param num_sensors Number of sensors in array
     * @param cal_cfg Calibration configuration
     */
    PressureSensorCalibrator(PressureSensor** sensors, 
                             size_t num_sensors,
                             const CalibrationConfig& cal_cfg) noexcept;

    /**
     * @brief Perform VA calibration using atmospheric sensors.
     *
     * Reads all sensors marked with use_for_va_cal=true, verifies they
     * agree within tolerance, and calculates VA = 2 × average_voltage.
     *
     * @return Calibrated VA voltage, or 0.0f if calibration failed
     */
    float CalibrateVA() noexcept;

    /**
     * @brief Apply calibrated VA to all sensors.
     * @param va Calibrated VA voltage
     */
    void ApplyVA(float va) noexcept;

    /**
     * @brief Perform zero calibration on all sensors.
     *
     * Each sensor stores its atmospheric offset for later correction.
     *
     * @return true if all sensors calibrated successfully
     */
    bool CalibrateAllZeros() noexcept;

    /**
     * @brief Perform swap calibration on all sensors.
     *
     * For each sensor, calls CalibrateSwap() with the specified duration.
     * This method is blocking — another task should actuate valves during
     * this time to create measurable pressure changes.
     *
     * @param duration_ms How long to sample each sensor
     * @param sample_interval_ms Delay between samples
     * @param apply_if_detected If true, auto-apply swap when detected
     * @return Number of sensors that successfully calibrated
     */
    size_t CalibrateAllSwaps(
        uint32_t duration_ms = 2000,
        uint32_t sample_interval_ms = 10,
        bool apply_if_detected = true) noexcept;

    /**
     * @brief Get last calibration error message.
     */
    const char* GetLastError() const noexcept { return last_error_; }

    /**
     * @brief Check if VA calibration succeeded.
     */
    bool IsVACalibrated() const noexcept { return va_calibrated_; }

    /**
     * @brief Get the calibrated VA value.
     */
    float GetCalibratedVA() const noexcept { return calibrated_va_; }

private:
    PressureSensor** sensors_;
    size_t num_sensors_;
    const CalibrationConfig& cal_config_;
    
    float calibrated_va_;
    bool va_calibrated_;
    const char* last_error_;
};
