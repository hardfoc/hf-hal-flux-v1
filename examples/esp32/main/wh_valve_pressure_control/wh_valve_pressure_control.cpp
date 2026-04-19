/**
 * @file wh_valve_pressure_control.cpp
 * @brief WH bench-test: pressure sensor calibration and high-rate monitoring.
 *
 * Hardware: 8× Honeywell SSC TruStability ratiometric pressure transducers
 * connected to an ADS7952 12-bit SPI ADC on the Flux V1 board:
 *   - CH0: DPS_LOW  — Differential ±0.18 PSI (low range differential)
 *   - CH1: DPS_HIGH — Differential ±2.32 PSI (high range differential)
 *   - CH2: PMS      — Single-ended 0–5 PSI (manifold supply)
 *   - CH3: PLS_LOW  — Single-ended 0–15 PSI (load sensing low)
 *   - CH4: PLS_HIGH — Single-ended 0–150 PSI (load sensing high)
 *   - CH5: RLS      — Single-ended 0 to -30 PSI (return line suction/vacuum)
 *   - CH6: PWS2     — Single-ended 0–1 PSI (pilot/work stage 2)
 *   - CH7: PWS1     — Single-ended 0–1 PSI (pilot/work stage 1)
 *
 * SSC sensor characteristics (per datasheet, transfer function "A"):
 *   - Output spans 10% – 90% of V_supply over the full scale (span = 0.8·VA).
 *   - Output clipping limits: 2.5% / 97.5% of V_supply.
 *   - Internal calibration & temperature compensation; ASIC updates
 *     pressure data at ~1 kHz with ~1 ms response time.
 *   - Resolution ≥ 0.03 % FSS, accuracy ±0.25 % FSS BFSL,
 *     total error band ±2 % FSS.
 *
 * Acquisition architecture:
 *   - Worker thread runs at 1 kHz (1 ms step) — matches the SSC ASIC update.
 *   - Each step issues ONE batched Auto-1 SPI transaction on the ADS7952
 *     for all 8 channels (handler `ReadMultipleChannels`).
 *   - Each channel gets a per-sensor single-pole IIR low-pass (default
 *     fc = 100 Hz at fs = 1 kHz, α ≈ 0.467) — strongly attenuates ADC and
 *     supply noise while preserving useful bandwidth for control.
 *   - Latest filtered voltage and pressure are published atomically per
 *     channel: control-loop threads at any rate (e.g. 500 Hz) call
 *     `PressureSensor::GetPressureFiltered()` lock-free.
 *
 * Calibration sequence (one-shot at boot):
 *   1. VA calibration — derive actual supply voltage from atmospheric sensors.
 *   2. Zero calibration — store per-sensor offset voltage at atmospheric.
 *   3. (Optional) Auto-swap detection — for single-ended sensors with
 *      reversed plumbing.
 *
 * @author HardFOC Team — WH Bench Test
 * @date 2026
 * @copyright HardFOC (internal use only)
 */

#include "PressureSensor.h"
#include "PressureSensorConfig.h"
#include "PressureSensorThread.h"
#include "WhDiagnosticsThread.h"
#include "WhValveControlThread.h"
#include "WhValveSystem.h"
#include "api/Flux.h"

#include "OsUtility.h"
#include "esp_log.h"
#include "freertos/task.h"

#include <array>
#include <atomic>

static const char* TAG_MAIN     = "WHControl";

//=============================================================================
// TASK CONFIGURATION
//=============================================================================

/// Pressure sampling interval (ms). 1 ms = 1 kHz, matches the SSC ASIC's
/// internal ~1 kHz pressure-data update rate. The ADS7952 batched read for
/// 8 channels takes well under 100 µs, leaving ample headroom.
static constexpr uint32_t kPressureSampleIntervalMs = 1;

/// IIR low-pass cutoff (Hz) applied per channel. 100 Hz at 1 kHz sampling
/// gives ~1.6 ms group delay — clean data for control loops at 200–500 Hz.
static constexpr float    kPressureFilterCutoffHz   = 100.0f;

/// Calibration-phase ADC averaging (used by Sample(); the live path uses
/// the IIR filter instead).
static constexpr uint8_t  kPressureAvgSamples       = 8;

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
        .ratio_range   = RatiometricRange::RANGE_10_90,
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
        .ratio_range   = RatiometricRange::RANGE_10_90,
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
        .ratio_range   = RatiometricRange::RANGE_10_90,
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
        .ratio_range   = RatiometricRange::RANGE_10_90,
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
        .ratio_range   = RatiometricRange::RANGE_10_90,
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
        .ratio_range   = RatiometricRange::RANGE_10_90,
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
        .ratio_range   = RatiometricRange::RANGE_10_90,
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
        .ratio_range   = RatiometricRange::RANGE_10_90,
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
    ESP_LOGI(TAG_MAIN, "Sensors: %zu Honeywell SSC channels on ADS7952 (SPI3)", kNumSensors);
    ESP_LOGI(TAG_MAIN, "Sampling: %lu ms (%.0f Hz) batched Auto-1, IIR LPF @ %.1f Hz",
             static_cast<unsigned long>(kPressureSampleIntervalMs),
             1000.0 / static_cast<double>(kPressureSampleIntervalMs),
             static_cast<double>(kPressureFilterCutoffHz));
    ESP_LOGI(TAG_MAIN, "Calibration averaging: %u ADC samples per call",
             kPressureAvgSamples);
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

    // Ensure worker-thread calibration logs are not filtered below INFO
    esp_log_level_set("Pressure", ESP_LOG_INFO);
    esp_log_level_set("PressureSensor", ESP_LOG_INFO);

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
        kPressureAvgSamples,
        kPressureFilterCutoffHz);

    ESP_LOGW(TAG_MAIN, "DBG: calling pressure_thread.EnsureInitialized()...");
    if (!pressure_thread.EnsureInitialized()) {
        ESP_LOGE(TAG_MAIN, "FATAL: Failed to initialize pressure thread");
        return;
    }
    ESP_LOGW(TAG_MAIN, "DBG: pressure thread initialized, starting...");
    // Allow time for the worker to reach MarkThreadRunning() (Setup can run long after that)
    if (!pressure_thread.StartThreadAndWaitToVerify(5000)) {
        ESP_LOGE(TAG_MAIN, "FATAL: Failed to start pressure thread (running flag not set in time)");
        return;
    }
    ESP_LOGW(TAG_MAIN, "DBG: StartThreadAndWaitToVerify OK — worker marked running; waiting for calibration...");

    // Wait for calibration to complete (Setup runs on worker; main must not spin silently)
    for (uint32_t w = 0; !g_pressure_task_ready.load(std::memory_order_acquire); ++w) {
        os_delay_msec(200);
        if ((w % 5U) == 0U) {
            ESP_LOGW(TAG_MAIN, "waiting for pressure calibration... (~%u s elapsed)",
                     static_cast<unsigned>((w + 1U) * 200U / 1000U));
        }
        taskYIELD();
    }
    ESP_LOGI(TAG_MAIN, "Pressure sensor task running (calibration finished)");
    ESP_LOGI(TAG_MAIN,
             "Control-loop access pattern (call from any thread, lock-free):");
    ESP_LOGI(TAG_MAIN,
             "    float p = g_sensors[i]->GetPressureFiltered();   // engineering units");
    ESP_LOGI(TAG_MAIN,
             "    float v = g_sensors[i]->GetVoltageFiltered();    // V (post-IIR)");

    // ── Bring up the WH valve subsystem (control + diagnostics threads) ────
    auto& valve_system  = whvalve::WhValveSystem::GetInstance();
    auto& valve_manager = ValveManager::GetInstance();
    auto* ads7952_drv = static_cast<Ads7952Handler*>(
        FLUX_API.adc.Get(HfFunctionalAdcChannel::ADS7952_0_CH0));

    // Configure the global MAX22200 plunger-movement detection algorithm
    // before any solenoid Apply() runs. The DPM_EN bit on the per-channel
    // CFG_CH register is pushed by SolenoidValve::ApplyChannelFeatures()
    // for every solenoid that opted in via its Max22200ChannelFeatures.
    if (valve_manager.IsInitialized()) {
        const auto dpm_err = valve_system.ConfigureGlobalDpm(
            valve_manager,
            /*start_current_ma =*/ 30.0f,    // start dip search above 30 mA
            /*dip_threshold_ma =*/ 8.0f,     // require ≥ 8 mA dip
            /*debounce_ms      =*/ 0.5f);    // 0.5 ms minimum dip duration
        if (dpm_err == hf_valve_err_t::VALVE_SUCCESS) {
            ESP_LOGI(TAG_MAIN, "MAX22200 DPM: ISTART=30 mA, IPTH=8 mA, TDEB=0.5 ms");
        } else {
            ESP_LOGW(TAG_MAIN, "MAX22200 DPM config skipped: %s",
                     HfValveErrToString(dpm_err));
        }
    }

    static whvalve::WhValveControlThread valve_ctrl(valve_system, valve_manager, /*ms=*/10);
    static whvalve::WhDiagnosticsThread  valve_diag(valve_system, valve_manager, ads7952_drv,
                                                    /*ms=*/100);

    if (!valve_ctrl.EnsureInitialized() ||
        !valve_ctrl.StartThreadAndWaitToVerify(2000)) {
        ESP_LOGE(TAG_MAIN, "Valve control thread failed to start");
    } else {
        ESP_LOGI(TAG_MAIN, "Valve control thread running (10 ms / 100 Hz, lock-free setpoints)");
    }

    if (!valve_diag.EnsureInitialized() ||
        !valve_diag.StartThreadAndWaitToVerify(2000)) {
        ESP_LOGE(TAG_MAIN, "Valve diagnostics thread failed to start");
    } else {
        ESP_LOGI(TAG_MAIN, "Valve diagnostics thread running (100 ms / 10 Hz)");
    }

    ESP_LOGI(TAG_MAIN, "Valve set:  %zu solenoids (incl. SEV pseudo-prop), %zu proportionals",
             valve_system.SolenoidCount(), valve_system.ProportionalCount());
    ESP_LOGI(TAG_MAIN, "Valve API (lock-free, callable from any thread):");
    ESP_LOGI(TAG_MAIN,
             "    valve_system.Get(SolenoidId::OLV_1).RequestState(true);");
    ESP_LOGI(TAG_MAIN,
             "    valve_system.Get(SolenoidId::SEV).RequestPosition(0.4f);  // pseudo-prop");
    ESP_LOGI(TAG_MAIN,
             "    valve_system.Get(ProportionalId::FCV).RequestPosition(0.5f);");
    ESP_LOGI(TAG_MAIN,
             "    auto snap = valve_system.Get(ProportionalId::FCV).Snapshot();");

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

        // ── Valve subsystem snapshot (system-wide health) ────────────────
        const auto diag = valve_diag.GetSnapshot();
        ESP_LOGI(TAG_MAIN,
                 "Valves: mgr=%s max22200=%s tle92466=%s ads7952=%s "
                 "apply_errors=%lu faults=%lu",
                 diag.manager_ready    ? "OK" : "DOWN",
                 diag.max22200_ready   ? "OK" : "DOWN",
                 diag.tle92466_ready   ? "OK" : "DOWN",
                 diag.ads7952_ready    ? "OK" : "DOWN",
                 static_cast<unsigned long>(diag.valve_total_apply_errors),
                 static_cast<unsigned long>(diag.valve_total_faults));

        // MAX22200 device-level health
        if (diag.max22200_ready) {
            ESP_LOGI(TAG_MAIN,
                     "  MAX22200: %sACTIVE channels=0x%02X  ovt=%d uvm=%d comer=%d "
                     "g_ocp=%d g_olf=%d g_hhf=%d g_dpm=%d",
                     diag.max_active ? "" : "!",
                     diag.max_channels_on_mask,
                     diag.max_overtemperature, diag.max_undervoltage,
                     diag.max_communication_error,
                     diag.max_global_overcurrent, diag.max_global_open_load,
                     diag.max_global_hit_not_reached,
                     diag.max_global_plunger_movement);
        }
        // TLE92466ED device-level health
        if (diag.tle92466_ready) {
            ESP_LOGI(TAG_MAIN,
                     "  TLE92466: vbat[uv=%d ov=%d] vio[uv=%d ov=%d] vdd[uv=%d ov=%d]"
                     " ot_warn=%d ot_err=%d clk=%d wd=%d ecc[reg=%d otp=%d]"
                     " sup_nok[int=%d ext=%d]",
                     diag.tle_vbat_uv, diag.tle_vbat_ov,
                     diag.tle_vio_uv,  diag.tle_vio_ov,
                     diag.tle_vdd_uv,  diag.tle_vdd_ov,
                     diag.tle_ot_warning, diag.tle_ot_error,
                     diag.tle_clock_fault, diag.tle_spi_wd_error,
                     diag.tle_reg_ecc_err, diag.tle_otp_ecc_err,
                     diag.tle_supply_nok_internal, diag.tle_supply_nok_external);
        }

        // Per-valve detail
        valve_system.ForEachSolenoid([](whvalve::SolenoidValve& v) {
            const auto s = v.Snapshot();
            ESP_LOGI(TAG_MAIN,
                     "  SOL %-6.*s [%-15s]: tgt=%4.2f act=%4.2f  state=%-12s  "
                     "fc=%lu  flags=[ocp=%d hhf=%d olf=%d dpm=%d]",
                     static_cast<int>(v.Label().size()), v.Label().data(),
                     whvalve::SolenoidDriveModeToString(v.DriveMode()),
                     static_cast<double>(v.GetTargetPosition()),
                     static_cast<double>(v.GetActualPosition()),
                     whvalve::ValveStateToString(v.GetState()),
                     static_cast<unsigned long>(s.fault_count),
                     s.overcurrent, s.hit_not_reached,
                     s.open_load,   s.plunger_movement_fault);
        });
        valve_system.ForEachProportional([](whvalve::ProportionalValve& v) {
            const auto s = v.Snapshot();
            ESP_LOGI(TAG_MAIN,
                     "  PROP %-6.*s: tgt=%4.2f act=%4.2f  state=%-12s  fc=%lu  "
                     "I_avg=%u duty=%u  flags=[oc=%d sg=%d ol=%d ot=%d olsg=%d]"
                     "  warn=[ot=%d cur_reg=%d pwm_reg=%d olsg=%d]",
                     static_cast<int>(v.Label().size()), v.Label().data(),
                     static_cast<double>(v.GetTargetPosition()),
                     static_cast<double>(v.GetActualPosition()),
                     whvalve::ValveStateToString(v.GetState()),
                     static_cast<unsigned long>(s.fault_count),
                     s.average_current_raw, s.duty_cycle_raw,
                     s.overcurrent, s.short_to_ground, s.open_load,
                     s.over_temperature, s.open_load_short_ground,
                     s.ot_warning, s.current_regulation_warning,
                     s.pwm_regulation_warning, s.olsg_warning);
        });
    }
}
