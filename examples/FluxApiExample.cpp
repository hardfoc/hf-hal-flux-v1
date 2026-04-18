/**
 * @file FluxApiExample.cpp
 * @brief Comprehensive documentation example: Full Flux API on Flux V1.
 *
 * Demonstrates the complete Flux singleton API, touching all 6 managers
 * in a single example that shows a realistic system startup, operation,
 * and monitoring sequence.
 *
 * Coverage:
 *   1. System initialization and diagnostics
 *   2. CommChannelsManager -- SPI device validation
 *   3. GpioManager -- pin queries and fault pin monitoring
 *   4. ValveManager -- solenoid CDR + proportional setup + operation
 *   5. AdcManager -- VBUS and NTC reads
 *   6. LedManager -- status indication tied to system state
 *   7. TemperatureManager -- thermal monitoring with protection
 *   8. Health monitoring loop -- periodic diagnostics + LED status
 *   9. Graceful shutdown
 *
 * This example is structured as a realistic application lifecycle:
 *   BOOT -> VALIDATE -> CONFIGURE -> OPERATE -> MONITOR -> SHUTDOWN
 *
 * @author HardFOC Team
 * @date 2025
 * @copyright HardFOC
 */

#include "api/Flux.h"

// Full manager headers (Flux.h only forward-declares them)
#include "managers/CommChannelsManager.h"
#include "managers/GpioManager.h"
#include "managers/ValveManager.h"
#include "managers/AdcManager.h"
#include "managers/LedManager.h"
#include "managers/TemperatureManager.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "FluxApiExample";

//=============================================================================
// Phase 1: System Boot and Validation
//=============================================================================

static bool phase_boot() {
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  PHASE 1: SYSTEM BOOT");
    ESP_LOGI(TAG, "========================================");

    auto& flux = Flux::GetInstance();

    // Show boot animation while initializing
    // Note: leds may not be available until after full init, so this is
    // just demonstrative -- in practice, init the LED manually first.

    // Initialize all subsystems
    bool ok = flux.EnsureInitialized();
    ESP_LOGI(TAG, "EnsureInitialized: %s", ok ? "SUCCESS" : "PARTIAL/FAILED");
    ESP_LOGI(TAG, "IsInitialized:     %s", flux.IsInitialized() ? "YES" : "NO");
    ESP_LOGI(TAG, "System version:    %s", flux.GetSystemVersion());
    ESP_LOGI(TAG, "Init time:         %llu ms",
             static_cast<unsigned long long>(flux.GetInitializationTimeMs()));

    // Check per-component initialization status
    auto status = flux.GetComponentInitializationStatus();
    const char* names[] = {"CommChannels", "GPIO", "Valves", "ADC", "LEDs", "Temperature"};
    for (size_t i = 0; i < status.size(); ++i) {
        ESP_LOGI(TAG, "  %-15s: %s", names[i], status[i] ? "OK" : "FAILED");
    }

    // Full diagnostics
    FluxSystemDiagnostics diag{};
    if (flux.GetSystemDiagnostics(diag)) {
        ESP_LOGI(TAG, "Diagnostics: %u/%u components OK, %u failed",
                 diag.initialized_components, diag.total_components,
                 diag.failed_components);
        if (diag.failed_components > 0) {
            for (size_t i = 0; i < diag.failed_components_list_count; ++i) {
                ESP_LOGW(TAG, "  FAILED: %s", diag.failed_components_list[i]);
            }
        }
    }

    return ok;
}

//=============================================================================
// Phase 2: Communication Validation
//=============================================================================

static bool phase_validate_comms() {
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  PHASE 2: COMMUNICATION VALIDATION");
    ESP_LOGI(TAG, "========================================");

    auto& comms = FLUX_API.comms;

    // Verify both SPI devices are accessible
    BaseSpi* max_dev = comms.GetSpiDevice(SpiDeviceId::MAX22200_SOLENOID_DRIVER);
    BaseSpi* tle_dev = comms.GetSpiDevice(SpiDeviceId::TLE92466_LOWSIDE_DRIVER);

    ESP_LOGI(TAG, "MAX22200 SPI device: %s", max_dev ? "OK" : "MISSING");
    ESP_LOGI(TAG, "TLE92466 SPI device: %s", tle_dev ? "OK" : "MISSING");

    // Comm diagnostics
    CommSystemDiagnostics cdiag{};
    if (comms.GetSystemDiagnostics(cdiag) == CommError::SUCCESS) {
        ESP_LOGI(TAG, "Comm system healthy: %s", cdiag.system_healthy ? "YES" : "NO");
        ESP_LOGI(TAG, "SPI devices found:   %u", cdiag.spi_device_count);
    }

    bool comms_ok = (max_dev != nullptr && tle_dev != nullptr);
    ESP_LOGI(TAG, "Communication validation: %s", comms_ok ? "PASS" : "FAIL");
    return comms_ok;
}

//=============================================================================
// Phase 3: GPIO and Fault Pin Check
//=============================================================================

static bool phase_check_faults() {
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  PHASE 3: GPIO & FAULT CHECK");
    ESP_LOGI(TAG, "========================================");

    auto& gpio = FLUX_API.gpio;

    ESP_LOGI(TAG, "Registered GPIO pins: %zu", gpio.Size());

    // Read hardware fault pins -- active-low with pull-ups
    bool max_fault_raw = false, tle_fault_raw = false;
    bool faults_ok = true;

    if (gpio.Read("GPIO_MAX22200_FAULT_N", max_fault_raw) == hf_gpio_err_t::GPIO_SUCCESS) {
        ESP_LOGI(TAG, "MAX22200 FAULT_N: %s", max_fault_raw ? "OK (HIGH)" : "FAULT (LOW)!");
        if (!max_fault_raw) faults_ok = false;
    }

    if (gpio.Read("GPIO_TLE92466_FAULT_N", tle_fault_raw) == hf_gpio_err_t::GPIO_SUCCESS) {
        ESP_LOGI(TAG, "TLE92466 FAULT_N: %s", tle_fault_raw ? "OK (HIGH)" : "FAULT (LOW)!");
        if (!tle_fault_raw) faults_ok = false;
    }

    // Verify driver enable pins are controllable
    ESP_LOGI(TAG, "GPIO_MAX22200_ENABLE: %s", gpio.Contains("GPIO_MAX22200_ENABLE") ? "found" : "MISSING");
    ESP_LOGI(TAG, "GPIO_TLE92466_EN:     %s", gpio.Contains("GPIO_TLE92466_EN") ? "found" : "MISSING");
    ESP_LOGI(TAG, "GPIO_USER_1:          %s", gpio.Contains("GPIO_USER_1") ? "found" : "MISSING");
    ESP_LOGI(TAG, "GPIO_USER_2:          %s", gpio.Contains("GPIO_USER_2") ? "found" : "MISSING");

    ESP_LOGI(TAG, "Fault check: %s", faults_ok ? "PASS (no faults)" : "WARNING (fault detected)");
    return faults_ok;
}

//=============================================================================
// Phase 4: Power Supply Validation
//=============================================================================

static bool phase_check_power() {
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  PHASE 4: POWER SUPPLY CHECK");
    ESP_LOGI(TAG, "========================================");

    auto& adc = FLUX_API.adc;

    // Read VBUS with averaging for stable reading
    float vbus = 0.0f;
    hf_adc_err_t err = adc.ReadVoltage("ADC_VBUS_MONITOR", vbus, 16);

    if (err == hf_adc_err_t::ADC_SUCCESS) {
        ESP_LOGI(TAG, "VBUS voltage: %.2f V (16-sample avg)", static_cast<double>(vbus));

        if (vbus < 10.0f) {
            ESP_LOGE(TAG, "VBUS too low -- valve drivers may not operate correctly");
            return false;
        }
        if (vbus > 30.0f) {
            ESP_LOGE(TAG, "VBUS too high -- overvoltage protection recommended");
            return false;
        }
        ESP_LOGI(TAG, "Power supply: OK");
    } else {
        ESP_LOGW(TAG, "VBUS read failed -- continuing without voltage check");
    }

    // NTC thermistor raw check
    float ntc_v = 0.0f;
    adc.ReadVoltage("ADC_NTC_THERMISTOR", ntc_v, 8);
    ESP_LOGI(TAG, "NTC voltage: %.3f V (expect 0.5-2.5V for 0-80C range)",
             static_cast<double>(ntc_v));

    return true;
}

//=============================================================================
// Phase 5: Valve Configuration and Operation
//=============================================================================

static void phase_valve_operation() {
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  PHASE 5: VALVE OPERATION");
    ESP_LOGI(TAG, "========================================");

    auto& valves = FLUX_API.valves;

    // Configure a solenoid channel (CDR mode)
    hf_valve_err_t err;
    err = valves.ConfigureSolenoidCdr(ValveChannel::SOL_CH0, 800, 200, 50.0f);
    ESP_LOGI(TAG, "Configure SOL_CH0 CDR (800/200mA, 50ms): %s", HfValveErrToString(err));

    // Configure a proportional channel with PWM + dither
    err = valves.ConfigureProportionalPwm(ValveChannel::PROP_CH0, 200.0f);
    ESP_LOGI(TAG, "Configure PROP_CH0 PWM (200Hz): %s", HfValveErrToString(err));

    err = valves.ConfigureProportionalDither(ValveChannel::PROP_CH0, 50.0f, 100.0f);
    ESP_LOGI(TAG, "Configure PROP_CH0 dither (50mA@100Hz): %s", HfValveErrToString(err));

    // Activate solenoid
    err = valves.EnableChannel(ValveChannel::SOL_CH0);
    ESP_LOGI(TAG, "Activate SOL_CH0: %s", HfValveErrToString(err));
    vTaskDelay(pdMS_TO_TICKS(200));

    // Ramp proportional current
    err = valves.EnableChannel(ValveChannel::PROP_CH0);
    ESP_LOGI(TAG, "Activate PROP_CH0: %s", HfValveErrToString(err));

    for (uint16_t ma = 0; ma <= 1000; ma += 250) {
        valves.SetProportionalCurrent(ValveChannel::PROP_CH0, ma);
        ESP_LOGI(TAG, "  PROP_CH0 current: %u mA", ma);
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    // Check for faults after operation
    if (valves.HasFault()) {
        ESP_LOGW(TAG, "Fault detected during valve operation!");
        valves.ClearAllFaults();
    } else {
        ESP_LOGI(TAG, "No faults during operation");
    }

    // Deactivate all
    valves.DisableAllChannels();
    ESP_LOGI(TAG, "All valves disabled");

    // Show valve diagnostics
    ValveSystemDiagnostics vdiag{};
    if (valves.GetSystemDiagnostics(vdiag)) {
        ESP_LOGI(TAG, "Valve stats: %u configured, %u active, %lu activations, %lu errors",
                 vdiag.configured_channels, vdiag.active_channels,
                 static_cast<unsigned long>(vdiag.total_activations),
                 static_cast<unsigned long>(vdiag.total_errors));
    }
}

//=============================================================================
// Phase 6: Health Monitoring Loop
//=============================================================================

static void phase_health_monitoring() {
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  PHASE 6: HEALTH MONITORING (3 cycles)");
    ESP_LOGI(TAG, "========================================");

    auto& flux = FLUX_API;

    for (int cycle = 0; cycle < 3; ++cycle) {
        ESP_LOGI(TAG, "--- Monitor Cycle %d ---", cycle);

        // Temperature check
        float esp32_c = 0.0f, ntc_c = 0.0f;
        flux.temp.ReadInternalTemperature(esp32_c);
        flux.temp.ReadNtcTemperature(ntc_c);
        ESP_LOGI(TAG, "  ESP32: %.1f C  NTC: %.1f C",
                 static_cast<double>(esp32_c), static_cast<double>(ntc_c));

        // VBUS check
        float vbus = 0.0f;
        flux.adc.ReadVoltage("ADC_VBUS_MONITOR", vbus, 8);
        ESP_LOGI(TAG, "  VBUS: %.2f V", static_cast<double>(vbus));

        // Manager health snapshot
        ManagerHealthSnapshot snapshot{};
        if (flux.CollectManagerHealth(snapshot)) {
            ESP_LOGI(TAG, "  Health: %zu managers, %zu with errors, all_healthy=%s",
                     snapshot.count, snapshot.managers_with_errors,
                     snapshot.all_healthy ? "YES" : "NO");

            for (size_t i = 0; i < snapshot.count; ++i) {
                const auto& entry = snapshot.entries[i];
                if (entry.has_error) {
                    ESP_LOGW(TAG, "    %s: ERROR (code %u)", entry.name, entry.last_error_code);
                }
            }
        }

        // Update LED status based on health
        if (ntc_c > 60.0f) {
            flux.leds.IndicateError();
        } else if (ntc_c > 45.0f || !snapshot.all_healthy) {
            flux.leds.IndicateWarning();
        } else {
            flux.leds.IndicateReady();
        }
        flux.leds.UpdateAnimation();

        // System-level health check
        bool healthy = flux.PerformHealthCheck();
        ESP_LOGI(TAG, "  PerformHealthCheck: %s", healthy ? "PASS" : "FAIL");

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

//=============================================================================
// Phase 7: System Statistics and Shutdown
//=============================================================================

static void phase_shutdown() {
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  PHASE 7: SHUTDOWN");
    ESP_LOGI(TAG, "========================================");

    auto& flux = FLUX_API;

    // Ensure all valves are off
    flux.valves.DisableAllChannels();

    // Turn off LED
    flux.leds.StopAnimation();
    flux.leds.TurnOff();

    // Final uptime
    ESP_LOGI(TAG, "System uptime: %llu ms",
             static_cast<unsigned long long>(flux.GetSystemUptimeMs()));

    // Dump comprehensive statistics from all managers
    ESP_LOGI(TAG, "--- Full System Statistics ---");
    flux.DumpSystemStatistics();

    ESP_LOGI(TAG, "Shutdown complete");
}

//=============================================================================
// app_main -- entry point
//=============================================================================

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "============================================================");
    ESP_LOGI(TAG, "  Flux API Comprehensive Example");
    ESP_LOGI(TAG, "  Lifecycle: BOOT -> VALIDATE -> OPERATE -> MONITOR -> SHUTDOWN");
    ESP_LOGI(TAG, "============================================================");

    // Phase 1: Boot
    bool boot_ok = phase_boot();
    if (!boot_ok) {
        ESP_LOGW(TAG, "Boot completed with warnings -- continuing");
    }
    vTaskDelay(pdMS_TO_TICKS(200));

    // Indicate boot in progress
    FLUX_API.leds.IndicateBoot();
    FLUX_API.leds.UpdateAnimation();

    // Phase 2: Validate communications
    phase_validate_comms();
    vTaskDelay(pdMS_TO_TICKS(200));

    // Phase 3: Check GPIO fault pins
    phase_check_faults();
    vTaskDelay(pdMS_TO_TICKS(200));

    // Phase 4: Power supply validation
    bool power_ok = phase_check_power();
    vTaskDelay(pdMS_TO_TICKS(200));

    if (!power_ok) {
        ESP_LOGE(TAG, "Power supply check failed -- skipping valve operation");
        FLUX_API.leds.IndicateError();
        FLUX_API.leds.UpdateAnimation();
        vTaskDelay(pdMS_TO_TICKS(2000));
    } else {
        // Phase 5: Valve operation
        FLUX_API.leds.IndicateReady();
        FLUX_API.leds.UpdateAnimation();
        phase_valve_operation();
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    // Phase 6: Health monitoring
    phase_health_monitoring();
    vTaskDelay(pdMS_TO_TICKS(200));

    // Phase 7: Shutdown
    phase_shutdown();

    ESP_LOGI(TAG, "============================================================");
    ESP_LOGI(TAG, "  Flux API comprehensive example complete");
    ESP_LOGI(TAG, "============================================================");
}
