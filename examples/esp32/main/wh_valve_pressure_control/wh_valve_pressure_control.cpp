/**
 * @file wh_valve_pressure_control.cpp
 * @brief WH bench-test: pressure sensor calibration and monitoring.
 *
 * Configured for the WH test bench with 8 pressure transducers:
 *   - CH0: DPS_LOW  — Differential ±0.18 PSI (low range differential)
 *   - CH1: DPS_HIGH — Differential ±2.32 PSI (high range differential)
 *   - CH2: PMS      — Single-ended 0–5 PSI (manifold supply)
 *   - CH3: PLS_LOW  — Single-ended 0–15 PSI (load sensing low)
 *   - CH4: PLS_HIGH — Single-ended 0–150 PSI (load sensing high)
 *   - CH5: RLS      — Single-ended 0 to -30 PSI (return line suction/vacuum)
 *   - CH6: PWS2     — Single-ended 0–1 PSI (pilot/work stage 2)
 *   - CH7: PWS1     — Single-ended 0–1 PSI (pilot/work stage 1)
 *
 * All sensors are ratiometric (5%-95% of VA output).
 *
 * Calibration sequence:
 *   1. VA calibration — determine actual supply voltage from atmospheric sensors
 *   2. Zero calibration — store per-sensor offset at atmospheric
 *   3. Auto-swap detection — for single-ended sensors, detect reversed lines
 *
 * @author HardFOC Team — WH Bench Test
 * @date 2026
 * @copyright HardFOC (internal use only)
 */

#include "PressureSensor.h"
#include "PressureSensorConfig.h"
#include "PressureSensorThread.h"
#include "api/Flux.h"

#include "OsUtility.h"
#include "esp_log.h"

#include <array>
#include <atomic>

static const char* TAG_MAIN     = "WHControl";

//=============================================================================
// TASK CONFIGURATION
//=============================================================================

/// Pressure sensor sampling interval (ms)
static constexpr uint32_t kPressureSampleIntervalMs = 10;

/// Number of ADC samples to average per pressure reading
static constexpr uint8_t kPressureAvgSamples = 8;

//=============================================================================
// CALIBRATION CONFIGURATION
//=============================================================================

static const CalibrationConfig kCalConfig = {
    .va_nominal = 5.0f,
    .vref_nominal = 2.5f,
    .va_min_valid = 4.0f,
    .va_max_valid = 5.5f,
    .atmospheric_voltage_tolerance = 0.25f,  // Allow 250mV deviation
    .calibration_samples = 64,
    .calibration_sample_delay_ms = 5,
    .swap_min_range_threshold = 0.05f,   // Need 5% of full scale change to detect
    .swap_direction_threshold = 0.03f,   // 3% of full scale to determine direction
};

//=============================================================================
// PRESSURE SENSOR CONFIGURATION — 8 channels as specified
//=============================================================================

/**
 * Sensor configurations:
 *
 * Differential sensors (DIR_EITHER):
 *   - Measure positive and negative pressure equally
 *   - pressure_min = -range, pressure_max = +range
 *   - No auto-swap possible; user must manually correct if needed
 *   - Used for VA calibration (output VA/2 at 0 differential)
 *
 * Single-ended positive (DIR_POSITIVE):
 *   - Measure gauge pressure from 0 to +max
 *   - pressure_min = 0, pressure_max = +full_scale
 *   - Auto-swap if reading negative indicates reversed lines
 *
 * Single-ended negative (DIR_NEGATIVE):
 *   - Measure vacuum from 0 to -max
 *   - pressure_min = 0, pressure_max = -full_scale
 *   - Auto-swap if reading positive indicates reversed lines
 */
static PressureSensorConfig kSensorConfigs[] = {
    // ═══════════════════════════════════════════════════════════════════════
    // CH0: DPS_LOW — Differential Low Range ±0.18 PSI
    // ═══════════════════════════════════════════════════════════════════════
    {
        .adc_channel   = HfFunctionalAdcChannel::ADS7952_0_CH0,
        .label         = "DPS_LOW",
        .ratio_range   = RatiometricRange::RANGE_5_95,
        .voltage_min   = 0.0f,  // Computed from VA
        .voltage_max   = 0.0f,  // Computed from VA
        .pressure_min  = -0.18f,
        .pressure_max  = +0.18f,
        .units         = "PSI",
        .expected_dir  = PressureDirection::DIR_EITHER,
        .auto_swap     = false,  // Differential — no auto-detect
        .use_for_va_cal = true,  // Use for VA calibration
    },
    
    // ═══════════════════════════════════════════════════════════════════════
    // CH1: DPS_HIGH — Differential High Range ±2.32 PSI
    // ═══════════════════════════════════════════════════════════════════════
    {
        .adc_channel   = HfFunctionalAdcChannel::ADS7952_0_CH1,
        .label         = "DPS_HIGH",
        .ratio_range   = RatiometricRange::RANGE_5_95,
        .voltage_min   = 0.0f,
        .voltage_max   = 0.0f,
        .pressure_min  = -2.32f,
        .pressure_max  = +2.32f,
        .units         = "PSI",
        .expected_dir  = PressureDirection::DIR_EITHER,
        .auto_swap     = false,
        .use_for_va_cal = true,
    },
    
    // ═══════════════════════════════════════════════════════════════════════
    // CH2: PMS — Manifold Supply 0–5 PSI (single-ended positive)
    // ═══════════════════════════════════════════════════════════════════════
    {
        .adc_channel   = HfFunctionalAdcChannel::ADS7952_0_CH2,
        .label         = "PMS",
        .ratio_range   = RatiometricRange::RANGE_5_95,
        .voltage_min   = 0.0f,
        .voltage_max   = 0.0f,
        .pressure_min  = 0.0f,
        .pressure_max  = 5.0f,
        .units         = "PSI",
        .expected_dir  = PressureDirection::DIR_POSITIVE,
        .auto_swap     = true,
        .use_for_va_cal = true,  // At atmospheric, reads ~0
    },
    
    // ═══════════════════════════════════════════════════════════════════════
    // CH3: PLS_LOW — Load Sensing Low 0–15 PSI (single-ended positive)
    // ═══════════════════════════════════════════════════════════════════════
    {
        .adc_channel   = HfFunctionalAdcChannel::ADS7952_0_CH3,
        .label         = "PLS_LOW",
        .ratio_range   = RatiometricRange::RANGE_5_95,
        .voltage_min   = 0.0f,
        .voltage_max   = 0.0f,
        .pressure_min  = 0.0f,
        .pressure_max  = 15.0f,
        .units         = "PSI",
        .expected_dir  = PressureDirection::DIR_POSITIVE,
        .auto_swap     = true,
        .use_for_va_cal = true,
    },
    
    // ═══════════════════════════════════════════════════════════════════════
    // CH4: PLS_HIGH — Load Sensing High 0–150 PSI (single-ended positive)
    // ═══════════════════════════════════════════════════════════════════════
    {
        .adc_channel   = HfFunctionalAdcChannel::ADS7952_0_CH4,
        .label         = "PLS_HIGH",
        .ratio_range   = RatiometricRange::RANGE_5_95,
        .voltage_min   = 0.0f,
        .voltage_max   = 0.0f,
        .pressure_min  = 0.0f,
        .pressure_max  = 150.0f,
        .units         = "PSI",
        .expected_dir  = PressureDirection::DIR_POSITIVE,
        .auto_swap     = true,
        .use_for_va_cal = true,
    },
    
    // ═══════════════════════════════════════════════════════════════════════
    // CH5: RLS — Return Line Suction 0 to -30 PSI (single-ended negative/vacuum)
    // ═══════════════════════════════════════════════════════════════════════
    {
        .adc_channel   = HfFunctionalAdcChannel::ADS7952_0_CH5,
        .label         = "RLS",
        .ratio_range   = RatiometricRange::RANGE_5_95,
        .voltage_min   = 0.0f,
        .voltage_max   = 0.0f,
        .pressure_min  = 0.0f,       // Zero at atmospheric
        .pressure_max  = -30.0f,     // Negative = vacuum direction
        .units         = "PSI",
        .expected_dir  = PressureDirection::DIR_NEGATIVE,
        .auto_swap     = true,
        .use_for_va_cal = true,
    },
    
    // ═══════════════════════════════════════════════════════════════════════
    // CH6: PWS2 — Pilot/Work Stage 2, 0–1 PSI (single-ended positive)
    // ═══════════════════════════════════════════════════════════════════════
    {
        .adc_channel   = HfFunctionalAdcChannel::ADS7952_0_CH6,
        .label         = "PWS2",
        .ratio_range   = RatiometricRange::RANGE_5_95,
        .voltage_min   = 0.0f,
        .voltage_max   = 0.0f,
        .pressure_min  = 0.0f,
        .pressure_max  = 1.0f,
        .units         = "PSI",
        .expected_dir  = PressureDirection::DIR_POSITIVE,
        .auto_swap     = true,
        .use_for_va_cal = true,
    },
    
    // ═══════════════════════════════════════════════════════════════════════
    // CH7: PWS1 — Pilot/Work Stage 1, 0–1 PSI (single-ended positive)
    // ═══════════════════════════════════════════════════════════════════════
    {
        .adc_channel   = HfFunctionalAdcChannel::ADS7952_0_CH7,
        .label         = "PWS1",
        .ratio_range   = RatiometricRange::RANGE_5_95,
        .voltage_min   = 0.0f,
        .voltage_max   = 0.0f,
        .pressure_min  = 0.0f,
        .pressure_max  = 1.0f,
        .units         = "PSI",
        .expected_dir  = PressureDirection::DIR_POSITIVE,
        .auto_swap     = true,
        .use_for_va_cal = true,
    },
};

static constexpr size_t kNumSensors = sizeof(kSensorConfigs) / sizeof(kSensorConfigs[0]);

//=============================================================================
// SHARED STATE
//=============================================================================

static std::atomic<bool> g_system_running{false};
static std::atomic<bool> g_pressure_task_ready{false};

/// Sensor instances (created in pressure task)
static PressureSensor* g_sensors[kNumSensors] = {};

/// Calibration state
static float g_calibrated_va = kCalConfig.va_nominal;
static bool g_calibration_complete = false;

//=============================================================================
// app_main — Entry Point
//=============================================================================

extern "C" void app_main(void) {
    ESP_LOGI(TAG_MAIN, "");
    ESP_LOGI(TAG_MAIN, "╔══════════════════════════════════════════════════════════╗");
    ESP_LOGI(TAG_MAIN, "║      WH Valve & Pressure Sensor Control                  ║");
    ESP_LOGI(TAG_MAIN, "║      8-Channel Ratiometric Pressure Monitoring           ║");
    ESP_LOGI(TAG_MAIN, "╚══════════════════════════════════════════════════════════╝");
    ESP_LOGI(TAG_MAIN, "");
    ESP_LOGI(TAG_MAIN, "Sensors: %zu pressure transducers on ADS7952 (SPI2)", kNumSensors);
    ESP_LOGI(TAG_MAIN, "Sample rate: %u ms, %u-sample averaging",
             kPressureSampleIntervalMs, kPressureAvgSamples);
    ESP_LOGI(TAG_MAIN, "");

    // ── Sensor Summary ──────────────────────────────────────────────────────
    ESP_LOGI(TAG_MAIN, "Configured sensors:");
    for (size_t i = 0; i < kNumSensors; ++i) {
        const auto& cfg = kSensorConfigs[i];
        ESP_LOGI(TAG_MAIN, "  CH%zu: %-10s  %+7.2f to %+7.2f %s  (%s%s)",
                 i,
                 cfg.label,
                 static_cast<double>(cfg.pressure_min),
                 static_cast<double>(cfg.pressure_max),
                 cfg.units,
                 DirectionToString(cfg.expected_dir),
                 cfg.auto_swap ? ", auto-swap" : "");
    }
    ESP_LOGI(TAG_MAIN, "");

    // ── Initialize Flux HAL ─────────────────────────────────────────────────
    auto& flux = Flux::GetInstance();
    ESP_LOGW(TAG_MAIN, "DBG: calling Flux::EnsureInitialized()...");
    if (!flux.EnsureInitialized()) {
        ESP_LOGE(TAG_MAIN, "FATAL: Flux initialization failed — aborting");
        return;
    }
    ESP_LOGW(TAG_MAIN, "DBG: Flux init returned OK");
    ESP_LOGI(TAG_MAIN, "Flux HAL initialized successfully");

    // ── Verify ADC channels are available ───────────────────────────────────
    ESP_LOGW(TAG_MAIN, "DBG: getting ADC manager...");
    auto& adc = FLUX_API.adc;
    ESP_LOGW(TAG_MAIN, "DBG: checking channels...");
    bool all_channels_ok = true;
    for (size_t i = 0; i < kNumSensors; ++i) {
        BaseAdc* drv = adc.Get(kSensorConfigs[i].adc_channel);
        if (!drv) {
            ESP_LOGE(TAG_MAIN, "  CH%zu %-10s: ADC driver MISSING",
                     i, kSensorConfigs[i].label);
            all_channels_ok = false;
        }
    }
    
    if (!all_channels_ok) {
        ESP_LOGE(TAG_MAIN, "FATAL: One or more ADC channels unavailable");
        return;
    }
    ESP_LOGW(TAG_MAIN, "DBG: channels OK");
    ESP_LOGI(TAG_MAIN, "All %zu ADC channels verified", kNumSensors);

    // ── Test ADC read from main_task before starting pressure task ──────────
    ESP_LOGW(TAG_MAIN, "DBG: testing ADC read...");
    {
        float test_voltage = 0.0f;
        hf_adc_err_t err = adc.ReadVoltage(
            kSensorConfigs[0].adc_channel, test_voltage, 1);
        if (err == hf_adc_err_t::ADC_SUCCESS) {
            ESP_LOGI(TAG_MAIN, "Main task ADC test: CH0=%.3fV OK", 
                     static_cast<double>(test_voltage));
        } else {
            ESP_LOGE(TAG_MAIN, "Main task ADC test FAILED: err=%d", 
                     static_cast<int>(err));
        }
    }

    // ── Launch pressure sensor thread (BaseThread) ─────────────────────────
    g_system_running.store(true, std::memory_order_release);

    PressureSensorThread::SharedState shared_state{
        g_system_running,
        g_pressure_task_ready,
        g_sensors,
        g_calibrated_va,
        g_calibration_complete,
    };

    static PressureSensorThread pressure_thread(
        kSensorConfigs,
        kNumSensors,
        kCalConfig,
        shared_state,
        kPressureSampleIntervalMs,
        kPressureAvgSamples);

    ESP_LOGW(TAG_MAIN, "DBG: calling pressure_thread.EnsureInitialized()...");
    if (!pressure_thread.EnsureInitialized()) {
        ESP_LOGE(TAG_MAIN, "FATAL: Failed to initialize pressure thread");
        return;
    }
    ESP_LOGW(TAG_MAIN, "DBG: pressure thread initialized, starting...");
    if (!pressure_thread.StartThreadAndWaitToVerify(1000)) {
        ESP_LOGE(TAG_MAIN, "FATAL: Failed to start pressure thread");
        return;
    }

    // Wait for calibration to complete
    while (!g_pressure_task_ready.load()) {
        os_delay_msec(100);
    }
    ESP_LOGI(TAG_MAIN, "Pressure sensor task running");

    // ── Main loop: periodic system status ───────────────────────────────────
    while (true) {
        os_delay_msec(30000);  // Status every 30 seconds

        ESP_LOGI(TAG_MAIN, "");
        ESP_LOGI(TAG_MAIN, "─── System Status ───");
        ESP_LOGI(TAG_MAIN, "VA: %.3f V  |  Calibration: %s",
                 static_cast<double>(g_calibrated_va),
                 g_calibration_complete ? "OK" : "INCOMPLETE");
        
        uint32_t total_samples = 0;
        uint32_t total_errors = 0;
        for (size_t i = 0; i < kNumSensors; ++i) {
            if (g_sensors[i]) {
                total_samples += g_sensors[i]->GetSampleCount();
                total_errors += g_sensors[i]->GetErrorCount();
            }
        }
        ESP_LOGI(TAG_MAIN, "Total samples: %lu  |  Errors: %lu",
                 static_cast<unsigned long>(total_samples),
                 static_cast<unsigned long>(total_errors));
    }
}
