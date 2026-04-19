/**
 * @file PressureSensorConfig.h
 * @brief Configuration types for ratiometric pressure sensors.
 *
 * Supports differential and single-ended pressure transducers with:
 *   - Ratiometric output (5%-95% or 10%-90% of VA)
 *   - Auto-swap detection for single-ended sensors
 *   - VA and atmospheric zero calibration
 *
 * @author HardFOC Team
 * @date 2026
 * @copyright HardFOC
 */

#pragma once

#include "managers/AdcManager.h"
#include <cstdint>

//=============================================================================
// ENUMERATIONS
//=============================================================================

/**
 * @brief Ratiometric pressure sensor output range.
 *
 * Industrial pressure transducers are typically ratiometric — their output
 * voltage scales with supply voltage (VA). This eliminates VA drift errors
 * as long as we know actual VA.
 *
 * Common output ranges (as percentage of VA):
 *   - 5%-95%:  Output = VA × (0.05 + 0.90 × pressure_ratio)
 *   - 10%-90%: Output = VA × (0.10 + 0.80 × pressure_ratio)
 *
 * At 0 differential pressure (atmospheric), output is at midpoint:
 *   - 5%-95%:  midpoint = VA × 0.50
 *   - 10%-90%: midpoint = VA × 0.50
 */
enum class RatiometricRange {
    RANGE_5_95,    ///< 5% – 95% of VA (0.05×VA to 0.95×VA)
    RANGE_10_90,   ///< 10% – 90% of VA (0.10×VA to 0.90×VA)
    CUSTOM,        ///< User-specified voltage_min/max (non-ratiometric)
};

/**
 * @brief Expected direction of pressure increase from atmospheric.
 *
 * For line swap detection and auto-correction:
 *   - DIR_POSITIVE: Expect positive readings (gauge pressure, supply lines)
 *   - DIR_NEGATIVE: Expect negative readings (vacuum, suction lines)
 *   - DIR_EITHER:   Bidirectional differential — can go either way
 *
 * Single-ended sensors (DIR_POSITIVE/DIR_NEGATIVE):
 *   - If auto_swap=true and reading is opposite direction, flip on-the-fly
 *   - If auto_swap=false and reading is opposite, set swap_error flag
 *
 * Differential sensors (DIR_EITHER):
 *   - No auto-detection; user must call SetSwapped() manually if needed
 */
enum class PressureDirection {
    DIR_POSITIVE,  ///< Expect positive readings (gauge pressure)
    DIR_NEGATIVE,  ///< Expect negative readings (vacuum/suction)
    DIR_EITHER,    ///< Bidirectional differential — no auto-detect
};

//=============================================================================
// CONFIGURATION STRUCTURES
//=============================================================================

/**
 * @brief Pressure sensor transfer function configuration.
 *
 * For ratiometric sensors, voltage_min/max are calculated from VA:
 *   - RANGE_5_95:  voltage_min = 0.05×VA, voltage_max = 0.95×VA
 *   - RANGE_10_90: voltage_min = 0.10×VA, voltage_max = 0.90×VA
 *
 * For CUSTOM range, voltage_min/max are used directly (absolute voltages).
 *
 * Pressure range interpretation:
 *   - For DIR_POSITIVE: pressure_min=0, pressure_max=full_scale
 *   - For DIR_NEGATIVE: pressure_min=0, pressure_max=-full_scale (use negative)
 *   - For DIR_EITHER:   pressure_min=-half_scale, pressure_max=+half_scale
 */
struct PressureSensorConfig {
    HfFunctionalAdcChannel adc_channel;  ///< Which ADS7952 channel
    const char* label;                   ///< Human-readable name
    RatiometricRange ratio_range;        ///< Ratiometric output range type
    float voltage_min;                   ///< For CUSTOM: sensor output at min pressure (V)
    float voltage_max;                   ///< For CUSTOM: sensor output at max pressure (V)
    float pressure_min;                  ///< Pressure at voltage_min (engineering units)
    float pressure_max;                  ///< Pressure at voltage_max (engineering units)
    const char* units;                   ///< Engineering units string
    PressureDirection expected_dir;      ///< Expected direction of pressure increase
    bool auto_swap;                      ///< If true, auto-flip when opposite direction detected
    bool use_for_va_cal;                 ///< If true, use this sensor for VA calibration
};

//=============================================================================
// CALIBRATION CONFIGURATION
//=============================================================================

/**
 * @brief Calibration parameters for ratiometric sensors.
 */
struct CalibrationConfig {
    /// Nominal expected VA (fallback if calibration fails)
    float va_nominal = 5.0f;
    
    /// Assumed Vref (REF5025 on board)
    float vref_nominal = 2.5f;
    
    /// Minimum acceptable VA — below indicates power supply problem
    float va_min_valid = 4.0f;
    
    /// Maximum acceptable VA — above indicates overvoltage
    float va_max_valid = 5.5f;
    
    /// Maximum voltage deviation between sensors at atmospheric (V)
    float atmospheric_voltage_tolerance = 0.20f;
    
    /// Number of samples to average during calibration
    uint32_t calibration_samples = 64;
    
    /// Delay between calibration samples (ms)
    uint32_t calibration_sample_delay_ms = 5;
    
    /// Minimum pressure change required to determine direction (fraction of full scale)
    /// If we don't see at least this much pressure change, we can't determine swap
    float swap_min_range_threshold = 0.05f;
    
    /// Threshold above zero for "definitely positive" or "definitely negative" detection
    /// (fraction of full scale). Readings within this range could be offset noise.
    float swap_direction_threshold = 0.03f;
};

//=============================================================================
// SWAP CALIBRATION RESULT
//=============================================================================

/**
 * @brief Result of swap detection calibration.
 *
 * Returned by CalibrateSwap() to indicate what was detected and what action
 * was taken. The measured_direction field indicates which direction the
 * pressure predominantly went during calibration.
 *
 * Detection logic:
 *   1. Sample for specified duration while valves may be actuated
 *   2. Track min/max raw pressure readings
 *   3. Determine which direction pressure predominantly went
 *   4. Compare to ground truth (expected_direction param OR configured direction)
 *   5. If opposite to ground truth → swap needed
 *
 * For differential sensors (DIR_EITHER config):
 *   - MUST provide expected_direction parameter to CalibrateSwap()
 *   - Otherwise cant_determine=true and no action taken
 */
struct SwapCalibrationResult {
    /// Minimum raw pressure reading seen during calibration
    float min_pressure_raw = 0.0f;
    
    /// Maximum raw pressure reading seen during calibration  
    float max_pressure_raw = 0.0f;
    
    /// Range of pressure change observed (max - min)
    float pressure_range = 0.0f;
    
    /// Number of samples taken during calibration
    uint32_t sample_count = 0;
    
    /// Direction that was predominantly measured during calibration
    PressureDirection measured_direction = PressureDirection::DIR_EITHER;
    
    /// Direction used as ground truth for comparison
    PressureDirection ground_truth_direction = PressureDirection::DIR_EITHER;
    
    /// True if pressure went predominantly opposite to ground truth
    bool swap_detected = false;
    
    /// True if swap was actually applied (auto_swap=true or forced)
    bool swap_applied = false;
    
    /// True if we couldn't determine direction (insufficient range or differential without expected)
    bool cant_determine = false;
    
    /// True if calibration completed successfully (even if cant_determine)
    bool success = false;
    
    /// Human-readable status or error message
    const char* message = nullptr;
};

//=============================================================================
// HELPER FUNCTIONS
//=============================================================================

/**
 * @brief Calculate voltage_min for a ratiometric sensor given actual VA.
 */
inline float GetRatiometricVoltageMin(RatiometricRange range, float va) {
    switch (range) {
        case RatiometricRange::RANGE_5_95:  return 0.05f * va;
        case RatiometricRange::RANGE_10_90: return 0.10f * va;
        default: return 0.0f;
    }
}

/**
 * @brief Calculate voltage_max for a ratiometric sensor given actual VA.
 */
inline float GetRatiometricVoltageMax(RatiometricRange range, float va) {
    switch (range) {
        case RatiometricRange::RANGE_5_95:  return 0.95f * va;
        case RatiometricRange::RANGE_10_90: return 0.90f * va;
        default: return 5.0f;
    }
}

/**
 * @brief Get direction string for logging.
 */
inline const char* DirectionToString(PressureDirection dir) {
    switch (dir) {
        case PressureDirection::DIR_POSITIVE: return "POSITIVE";
        case PressureDirection::DIR_NEGATIVE: return "NEGATIVE";
        case PressureDirection::DIR_EITHER:   return "DIFFERENTIAL";
    }
    return "UNKNOWN";
}
