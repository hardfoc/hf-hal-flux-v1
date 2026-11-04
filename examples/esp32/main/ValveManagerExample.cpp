/**
 * @file ValveManagerExample.cpp
 * @brief Documentation example: Unified valve control on Flux V1.
 *
 * Demonstrates the ValveManager API for the Flux valve-control board:
 *   - Solenoid control via MAX22200 (8 channels: SOL_CH0..SOL_CH7)
 *     - CDR (current drive regulation): HIT/HOLD current profiles
 *     - VDR (voltage drive regulation): HIT/HOLD duty cycle profiles
 *     - Enable/disable individual channels
 *   - Proportional control via TLE92466ED (6 channels: PROP_CH0..PROP_CH5)
 *     - Current setpoint control (0-2000 mA per channel)
 *     - PWM frequency configuration (~30 Hz to 8 MHz)
 *     - Dither configuration (reduces valve stiction)
 *   - Unified operations: bitmask enable, disable-all, channel queries
 *   - Fault management: detection, reading, clearing
 *   - Channel state inspection and diagnostics
 *   - Direct driver access for advanced operations
 *
 * Hardware context:
 *   - MAX22200: 8-channel octal solenoid/motor driver (SPI, CS GPIO38)
 *   - TLE92466ED: 6-channel low-side driver with current regulation (SPI, CS GPIO4)
 *   - All 14 channels addressable through a single ValveManager API
 *
 * @author HardFOC Team
 * @date 2025
 * @copyright HardFOC
 */

#include "api/Flux.h"
#include "managers/ValveManager.h"

#include "esp_log.h"
#include "OsUtility.h"
static const char* TAG = "ValveExample";

/// Helper: log a valve error result
static void log_valve_result(const char* operation, hf_valve_err_t err) {
    if (HfValveErrOk(err)) {
        ESP_LOGI(TAG, "  %s: OK", operation);
    } else {
        ESP_LOGW(TAG, "  %s: %s (code %u)", operation,
                 HfValveErrToString(err), static_cast<unsigned>(err));
    }
}

// ---------------------------------------------------------------------------
// Example 1: Solenoid CDR configuration (current-drive regulation)
// ---------------------------------------------------------------------------
static void example_solenoid_cdr() {
    ESP_LOGI(TAG, "=== Example 1: Solenoid CDR Configuration ===");

    auto& valves = FLUX_API.valves;
    hf_valve_err_t err;

    // CDR mode configures HIT (inrush) and HOLD (sustain) current levels.
    // Typical solenoid: 800 mA hit for fast pull-in, 200 mA hold to reduce heat.

    // Configure SOL_CH0 with CDR: 800 mA hit, 200 mA hold, 50 ms hit time
    err = valves.ConfigureSolenoidCdr(ValveChannel::SOL_CH0,
                                       800,    // hit_ma
                                       200,    // hold_ma
                                       50.0f); // hit_time_ms
    log_valve_result("ConfigureSolenoidCdr(CH0, 800/200mA, 50ms)", err);

    // Configure SOL_CH1 with a gentler profile for a smaller solenoid
    err = valves.ConfigureSolenoidCdr(ValveChannel::SOL_CH1,
                                       500,    // hit_ma
                                       100,    // hold_ma
                                       30.0f); // hit_time_ms
    log_valve_result("ConfigureSolenoidCdr(CH1, 500/100mA, 30ms)", err);

    // Enable the channels to activate them
    err = valves.EnableChannel(ValveChannel::SOL_CH0);
    log_valve_result("EnableChannel(SOL_CH0)", err);

    os_delay_msec(200);

    // Disable after use
    err = valves.DisableChannel(ValveChannel::SOL_CH0);
    log_valve_result("DisableChannel(SOL_CH0)", err);
}

// ---------------------------------------------------------------------------
// Example 2: Solenoid VDR configuration (voltage-drive regulation)
// ---------------------------------------------------------------------------
static void example_solenoid_vdr() {
    ESP_LOGI(TAG, "=== Example 2: Solenoid VDR Configuration ===");

    auto& valves = FLUX_API.valves;
    hf_valve_err_t err;

    // VDR mode uses duty-cycle percentages instead of current targets.
    // Useful when the solenoid impedance is well-characterized.

    // Configure SOL_CH2 with VDR: 100% hit, 30% hold, 40 ms hit time
    err = valves.ConfigureSolenoidVdr(ValveChannel::SOL_CH2,
                                       100.0f,  // hit_duty_pct
                                       30.0f,   // hold_duty_pct
                                       40.0f);  // hit_time_ms
    log_valve_result("ConfigureSolenoidVdr(CH2, 100%/30%, 40ms)", err);

    // Activate briefly
    err = valves.EnableChannel(ValveChannel::SOL_CH2);
    log_valve_result("EnableChannel(SOL_CH2)", err);

    os_delay_msec(150);

    err = valves.DisableChannel(ValveChannel::SOL_CH2);
    log_valve_result("DisableChannel(SOL_CH2)", err);
}

// ---------------------------------------------------------------------------
// Example 3: Proportional valve current control
// ---------------------------------------------------------------------------
static void example_proportional_current() {
    ESP_LOGI(TAG, "=== Example 3: Proportional Current Control ===");

    auto& valves = FLUX_API.valves;
    hf_valve_err_t err;

    // Proportional valves use continuous current control.
    // The TLE92466ED regulates current per channel.

    // Enable the proportional channel first
    err = valves.EnableChannel(ValveChannel::PROP_CH0);
    log_valve_result("EnableChannel(PROP_CH0)", err);

    // Ramp current from 0 to 1000 mA in steps
    const uint16_t steps[] = {0, 200, 400, 600, 800, 1000};
    for (uint16_t ma : steps) {
        err = valves.SetProportionalCurrent(ValveChannel::PROP_CH0, ma);
        ESP_LOGI(TAG, "  SetProportionalCurrent(%u mA): %s",
                 ma, HfValveErrOk(err) ? "OK" : HfValveErrToString(err));
        os_delay_msec(100);
    }

    // Ramp back down
    for (int i = 4; i >= 0; --i) {
        err = valves.SetProportionalCurrent(ValveChannel::PROP_CH0, steps[i]);
        os_delay_msec(100);
    }

    err = valves.DisableChannel(ValveChannel::PROP_CH0);
    log_valve_result("DisableChannel(PROP_CH0)", err);
}

// ---------------------------------------------------------------------------
// Example 4: Proportional PWM and dither configuration
// ---------------------------------------------------------------------------
static void example_proportional_pwm_dither() {
    ESP_LOGI(TAG, "=== Example 4: PWM and Dither Configuration ===");

    auto& valves = FLUX_API.valves;
    hf_valve_err_t err;

    // Configure PWM frequency for PROP_CH1
    // Standard hydraulic proportional valves typically use 100-400 Hz
    err = valves.ConfigureProportionalPwm(ValveChannel::PROP_CH1, 200.0f);
    log_valve_result("ConfigureProportionalPwm(CH1, 200 Hz)", err);

    // Configure dither to reduce valve stiction
    // Small oscillation (50 mA peak-to-peak) at 100 Hz superimposed on setpoint
    err = valves.ConfigureProportionalDither(ValveChannel::PROP_CH1,
                                              50.0f,   // amplitude_ma
                                              100.0f); // frequency_hz
    log_valve_result("ConfigureProportionalDither(CH1, 50mA @ 100Hz)", err);

    // Set the operating current
    err = valves.EnableChannel(ValveChannel::PROP_CH1);
    log_valve_result("EnableChannel(PROP_CH1)", err);

    err = valves.SetProportionalCurrent(ValveChannel::PROP_CH1, 750);
    log_valve_result("SetProportionalCurrent(CH1, 750 mA)", err);

    os_delay_msec(500);

    err = valves.DisableChannel(ValveChannel::PROP_CH1);
    log_valve_result("DisableChannel(PROP_CH1)", err);
}

// ---------------------------------------------------------------------------
// Example 5: Bitmask enable and disable-all
// ---------------------------------------------------------------------------
static void example_bitmask_operations() {
    ESP_LOGI(TAG, "=== Example 5: Bitmask Enable / Disable-All ===");

    auto& valves = FLUX_API.valves;
    hf_valve_err_t err;

    // EnableChannelsMask: lower 8 bits = MAX22200, upper 6 bits = TLE92466
    // Enable SOL_CH0 + SOL_CH1 + PROP_CH0 simultaneously
    // SOL_CH0 = bit 0, SOL_CH1 = bit 1, PROP_CH0 = bit 8
    uint16_t mask = (1 << 0) | (1 << 1) | (1 << 8);
    err = valves.EnableChannelsMask(mask);
    log_valve_result("EnableChannelsMask(SOL0 + SOL1 + PROP0)", err);

    os_delay_msec(200);

    // Disable everything at once
    err = valves.DisableAllChannels();
    log_valve_result("DisableAllChannels()", err);
}

// ---------------------------------------------------------------------------
// Example 6: Channel state and info queries
// ---------------------------------------------------------------------------
static void example_channel_queries() {
    ESP_LOGI(TAG, "=== Example 6: Channel State Queries ===");

    auto& valves = FLUX_API.valves;

    // Check if a channel is enabled
    bool enabled = valves.IsChannelEnabled(ValveChannel::SOL_CH0);
    ESP_LOGI(TAG, "SOL_CH0 enabled: %s", enabled ? "YES" : "NO");

    // Get per-channel runtime state
    const ValveChannelState* state = valves.GetChannelState(ValveChannel::PROP_CH0);
    if (state != nullptr) {
        ESP_LOGI(TAG, "PROP_CH0 state:");
        ESP_LOGI(TAG, "  enabled:          %s", state->enabled ? "YES" : "NO");
        ESP_LOGI(TAG, "  configured:       %s", state->configured ? "YES" : "NO");
        ESP_LOGI(TAG, "  current_ma:       %u", state->current_ma);
        ESP_LOGI(TAG, "  pwm_frequency_hz: %.1f", static_cast<double>(state->pwm_frequency_hz));
        ESP_LOGI(TAG, "  dither:           %.1f mA @ %.1f Hz",
                 static_cast<double>(state->dither_amplitude_ma),
                 static_cast<double>(state->dither_frequency_hz));
        ESP_LOGI(TAG, "  activations:      %lu", static_cast<unsigned long>(state->activation_count));
        ESP_LOGI(TAG, "  errors:           %lu", static_cast<unsigned long>(state->error_count));
    }

    // Find a channel by name
    ValveChannel ch = valves.FindChannel("SOL_CH3");
    if (ch != ValveChannel::CHANNEL_COUNT) {
        ESP_LOGI(TAG, "Found SOL_CH3 as channel %u", static_cast<unsigned>(ch));
    }

    // Get compile-time channel info
    const ValveChannelInfo* info = valves.GetChannelInfo("PROP_CH2");
    if (info != nullptr) {
        ESP_LOGI(TAG, "PROP_CH2 info: driver=%s, hw_channel=%u",
                 (info->driver_type == ValveDriverType::TLE92466) ? "TLE92466" : "MAX22200",
                 info->hw_channel);
    }
}

// ---------------------------------------------------------------------------
// Example 7: Fault detection and management
// ---------------------------------------------------------------------------
static void example_fault_management() {
    ESP_LOGI(TAG, "=== Example 7: Fault Management ===");

    auto& valves = FLUX_API.valves;
    hf_valve_err_t err;

    // Check for faults on either driver
    bool any_fault = valves.HasFault();
    ESP_LOGI(TAG, "Any fault detected: %s", any_fault ? "YES" : "NO");

    // Check driver-specific faults
    bool sol_fault = valves.HasSolenoidFault();
    bool prop_fault = valves.HasProportionalFault();
    ESP_LOGI(TAG, "MAX22200 fault: %s", sol_fault ? "YES" : "NO");
    ESP_LOGI(TAG, "TLE92466 fault: %s", prop_fault ? "YES" : "NO");

    // Read detailed MAX22200 fault status
    max22200::FaultStatus max_faults{};
    err = valves.GetSolenoidFaults(max_faults);
    if (HfValveErrOk(err)) {
        ESP_LOGI(TAG, "MAX22200 fault register read: OK");
    }

    // Read detailed TLE92466ED fault report
    tle92466ed::FaultReport tle_faults{};
    err = valves.GetProportionalFaults(tle_faults);
    if (HfValveErrOk(err)) {
        ESP_LOGI(TAG, "TLE92466 fault report read: OK");
    }

    // Clear all faults (both drivers)
    if (any_fault) {
        err = valves.ClearAllFaults();
        log_valve_result("ClearAllFaults()", err);
    }
}

// ---------------------------------------------------------------------------
// Example 8: Locked chip access (advanced)
// ---------------------------------------------------------------------------
static void example_direct_driver_access() {
    ESP_LOGI(TAG, "=== Example 8: Locked Chip Access ===");

    auto& valves = FLUX_API.valves;

    // For operations not exposed through the unified API, use the locked access objects: each
    // call takes the chip handler's mutex, and WithDriver() runs driver calls under it. There is
    // no raw driver pointer (it would skip the mutex).
    if (auto max = valves.Max22200()) {
        uint32_t status = 0;
        const auto rs = max.WithDriver([&status](auto& drv) {
            return drv.ReadRegister32(max22200::RegBank::STATUS, status);
        });
        ESP_LOGI(TAG, "MAX22200 STATUS: 0x%08lX (%s)", static_cast<unsigned long>(status),
                 rs == max22200::DriverStatus::OK ? "ok" : "read failed");
    }

    if (auto tle = valves.Tle92466()) {
        ESP_LOGI(TAG, "TLE92466ED ICVID: 0x%04lX", static_cast<unsigned long>(tle.GetIcVersion()));
    }
}

// ---------------------------------------------------------------------------
// Example 9: Diagnostics
// ---------------------------------------------------------------------------
static void example_diagnostics() {
    ESP_LOGI(TAG, "=== Example 9: Valve System Diagnostics ===");

    auto& valves = FLUX_API.valves;

    ValveSystemDiagnostics diag{};
    if (valves.GetSystemDiagnostics(diag) == hf_valve_err_t::VALVE_SUCCESS) {
        ESP_LOGI(TAG, "System healthy:        %s", diag.system_healthy ? "YES" : "NO");
        ESP_LOGI(TAG, "MAX22200 initialized:  %s", diag.max22200_initialized ? "YES" : "NO");
        ESP_LOGI(TAG, "TLE92466 initialized:  %s", diag.tle92466_initialized ? "YES" : "NO");
        ESP_LOGI(TAG, "Total channels:        %u", diag.total_channels);
        ESP_LOGI(TAG, "Configured channels:   %u", diag.configured_channels);
        ESP_LOGI(TAG, "Active channels:       %u", diag.active_channels);
        ESP_LOGI(TAG, "Total activations:     %lu", static_cast<unsigned long>(diag.total_activations));
        ESP_LOGI(TAG, "Total errors:          %lu", static_cast<unsigned long>(diag.total_errors));
    }

    // Full statistics dump to logger
    valves.DumpStatistics();
}

// ---------------------------------------------------------------------------
// app_main -- entry point
// ---------------------------------------------------------------------------
extern "C" void app_main(void) {
    ESP_LOGI(TAG, "Flux ValveManager Example");
    ESP_LOGI(TAG, "=========================");

    auto& flux = Flux::GetInstance();
    if (!flux.EnsureInitialized()) {
        ESP_LOGE(TAG, "Flux initialization failed -- valve subsystem may be unavailable");
    }

    example_solenoid_cdr();
    os_delay_msec(200);

    example_solenoid_vdr();
    os_delay_msec(200);

    example_proportional_current();
    os_delay_msec(200);

    example_proportional_pwm_dither();
    os_delay_msec(200);

    example_bitmask_operations();
    os_delay_msec(200);

    example_channel_queries();
    os_delay_msec(200);

    example_fault_management();
    os_delay_msec(200);

    example_direct_driver_access();
    os_delay_msec(200);

    example_diagnostics();

    // Safety: ensure all channels are off when done
    FLUX_API.valves.DisableAllChannels();

    ESP_LOGI(TAG, "=========================");
    ESP_LOGI(TAG, "ValveManager example complete");
}
