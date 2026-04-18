/**
 * @file TemperatureManagerExample.cpp
 * @brief Documentation example: Temperature monitoring on Flux V1.
 *
 * Demonstrates the TemperatureManager API for the Flux valve-control board:
 *   - Reading ESP32-S3 internal temperature sensor (on-chip)
 *   - Reading NTC thermistor temperature (via ADC, GPIO4)
 *   - Celsius and Fahrenheit readings
 *   - Convenience methods: ReadInternalTemperature, ReadNtcTemperature
 *   - System temperature statistics (min/max/avg across all sensors)
 *   - Sensor info and entry queries
 *   - Periodic monitoring loop with threshold alerting
 *   - Diagnostics snapshot
 *
 * Flux V1 temperature sensors:
 *   - "ESP32_INTERNAL"  -- on-chip temperature (typ. 30-60 C under load)
 *   - "NTC_THERMISTOR"  -- external NTC probe (board/ambient temperature)
 *
 * @author HardFOC Team
 * @date 2025
 * @copyright HardFOC
 */

#include "api/Flux.h"
#include "managers/TemperatureManager.h"
#include "managers/ValveManager.h"
#include "managers/LedManager.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "TempExample";

// ---------------------------------------------------------------------------
// Example 1: Basic temperature reading
// ---------------------------------------------------------------------------
static void example_basic_reading() {
    ESP_LOGI(TAG, "=== Example 1: Basic Temperature Reading ===");

    auto& temp = FLUX_API.temp;

    // Read by sensor name (string-based)
    float internal_c = 0.0f;
    auto err = temp.ReadTemperatureCelsius("ESP32_INTERNAL", internal_c);
    if (err == TemperatureError::SUCCESS) {
        ESP_LOGI(TAG, "ESP32 internal: %.1f C", static_cast<double>(internal_c));
    } else {
        ESP_LOGW(TAG, "ESP32 internal read failed");
    }

    float ntc_c = 0.0f;
    err = temp.ReadTemperatureCelsius("NTC_THERMISTOR", ntc_c);
    if (err == TemperatureError::SUCCESS) {
        ESP_LOGI(TAG, "NTC thermistor: %.1f C", static_cast<double>(ntc_c));
    } else {
        ESP_LOGW(TAG, "NTC thermistor read failed");
    }
}

// ---------------------------------------------------------------------------
// Example 2: Convenience methods
// ---------------------------------------------------------------------------
static void example_convenience_methods() {
    ESP_LOGI(TAG, "=== Example 2: Convenience Methods ===");

    auto& temp = FLUX_API.temp;

    // Direct convenience accessors (no string lookup needed)
    float internal_c = 0.0f;
    if (temp.ReadInternalTemperature(internal_c) == TemperatureError::SUCCESS) {
        ESP_LOGI(TAG, "ReadInternalTemperature: %.1f C", static_cast<double>(internal_c));
    }

    float ntc_c = 0.0f;
    if (temp.ReadNtcTemperature(ntc_c) == TemperatureError::SUCCESS) {
        ESP_LOGI(TAG, "ReadNtcTemperature:     %.1f C", static_cast<double>(ntc_c));
    }
}

// ---------------------------------------------------------------------------
// Example 3: Fahrenheit readings
// ---------------------------------------------------------------------------
static void example_fahrenheit() {
    ESP_LOGI(TAG, "=== Example 3: Fahrenheit Readings ===");

    auto& temp = FLUX_API.temp;

    float internal_f = 0.0f;
    if (temp.ReadTemperatureFahrenheit("ESP32_INTERNAL", internal_f) == TemperatureError::SUCCESS) {
        ESP_LOGI(TAG, "ESP32 internal: %.1f F", static_cast<double>(internal_f));
    }

    float ntc_f = 0.0f;
    if (temp.ReadTemperatureFahrenheit("NTC_THERMISTOR", ntc_f) == TemperatureError::SUCCESS) {
        ESP_LOGI(TAG, "NTC thermistor: %.1f F", static_cast<double>(ntc_f));
    }
}

// ---------------------------------------------------------------------------
// Example 4: System temperature statistics
// ---------------------------------------------------------------------------
static void example_temperature_stats() {
    ESP_LOGI(TAG, "=== Example 4: Temperature Statistics ===");

    auto& temp = FLUX_API.temp;

    float min_c = 0.0f, max_c = 0.0f, avg_c = 0.0f;
    if (temp.GetSystemTemperatureStats(min_c, max_c, avg_c) == TemperatureError::SUCCESS) {
        ESP_LOGI(TAG, "Across all sensors:");
        ESP_LOGI(TAG, "  Min:  %.1f C", static_cast<double>(min_c));
        ESP_LOGI(TAG, "  Max:  %.1f C", static_cast<double>(max_c));
        ESP_LOGI(TAG, "  Avg:  %.1f C", static_cast<double>(avg_c));
    } else {
        ESP_LOGW(TAG, "Could not get temperature statistics");
    }
}

// ---------------------------------------------------------------------------
// Example 5: Sensor info queries
// ---------------------------------------------------------------------------
static void example_sensor_info() {
    ESP_LOGI(TAG, "=== Example 5: Sensor Info ===");

    auto& temp = FLUX_API.temp;

    ESP_LOGI(TAG, "Total sensors: %zu", temp.GetSensorCount());

    // Query per-sensor metadata
    const char* sensor_names[] = {"ESP32_INTERNAL", "NTC_THERMISTOR"};
    for (const char* name : sensor_names) {
        const TempEntry* entry = temp.GetSensorEntry(name);
        if (entry != nullptr) {
            ESP_LOGI(TAG, "  Sensor '%.*s':", static_cast<int>(entry->name.size()), entry->name.data());
            ESP_LOGI(TAG, "    type:         %s", FluxTempSensorTypeToString(entry->sensor_type));
            ESP_LOGI(TAG, "    registered:   %s", entry->registered ? "YES" : "NO");
            ESP_LOGI(TAG, "    last reading: %.1f C", static_cast<double>(entry->last_reading_celsius));
            ESP_LOGI(TAG, "    read count:   %lu", static_cast<unsigned long>(entry->read_count));
            ESP_LOGI(TAG, "    error count:  %lu", static_cast<unsigned long>(entry->error_count));
        } else {
            ESP_LOGW(TAG, "  Sensor '%s': NOT FOUND", name);
        }
    }
}

// ---------------------------------------------------------------------------
// Example 6: Periodic monitoring with threshold alerts
// ---------------------------------------------------------------------------
static void example_monitoring_loop() {
    ESP_LOGI(TAG, "=== Example 6: Monitoring Loop (5 iterations) ===");

    auto& temp = FLUX_API.temp;

    // Define thresholds
    static constexpr float ESP32_WARN_THRESHOLD_C = 70.0f;
    static constexpr float NTC_WARN_THRESHOLD_C   = 50.0f;

    for (int i = 0; i < 5; ++i) {
        float esp32_c = 0.0f, ntc_c = 0.0f;

        auto esp32_err = temp.ReadInternalTemperature(esp32_c);
        auto ntc_err   = temp.ReadNtcTemperature(ntc_c);

        ESP_LOGI(TAG, "  [%d] ESP32=%.1f C  NTC=%.1f C", i,
                 static_cast<double>(esp32_c), static_cast<double>(ntc_c));

        // Threshold alerting
        if (esp32_err == TemperatureError::SUCCESS && esp32_c > ESP32_WARN_THRESHOLD_C) {
            ESP_LOGW(TAG, "  WARNING: ESP32 temperature above %.0f C!", 
                     static_cast<double>(ESP32_WARN_THRESHOLD_C));
        }
        if (ntc_err == TemperatureError::SUCCESS && ntc_c > NTC_WARN_THRESHOLD_C) {
            ESP_LOGW(TAG, "  WARNING: Board temperature above %.0f C!",
                     static_cast<double>(NTC_WARN_THRESHOLD_C));
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

// ---------------------------------------------------------------------------
// Example 7: Integrated thermal protection pattern
// ---------------------------------------------------------------------------
static void example_thermal_protection() {
    ESP_LOGI(TAG, "=== Example 7: Thermal Protection Pattern ===");

    auto& flux = FLUX_API;

    // Real-world pattern: monitor temperature and adjust LED status
    float ntc_c = 0.0f;
    if (flux.temp.ReadNtcTemperature(ntc_c) == TemperatureError::SUCCESS) {
        if (ntc_c > 60.0f) {
            // Critical -- shut down valves, indicate error
            flux.valves.DisableAllChannels();
            flux.leds.IndicateError();
            ESP_LOGE(TAG, "CRITICAL: %.1f C -- valves disabled!", static_cast<double>(ntc_c));
        } else if (ntc_c > 45.0f) {
            // Warning -- visual indication
            flux.leds.IndicateWarning();
            ESP_LOGW(TAG, "Elevated temperature: %.1f C", static_cast<double>(ntc_c));
        } else {
            // Normal operation
            flux.leds.IndicateReady();
            ESP_LOGI(TAG, "Temperature normal: %.1f C", static_cast<double>(ntc_c));
        }
    }

    // Update the animation once
    flux.leds.UpdateAnimation();
    vTaskDelay(pdMS_TO_TICKS(500));

    flux.leds.StopAnimation();
    flux.leds.TurnOff();
}

// ---------------------------------------------------------------------------
// Example 8: Diagnostics
// ---------------------------------------------------------------------------
static void example_diagnostics() {
    ESP_LOGI(TAG, "=== Example 8: Temperature Diagnostics ===");

    auto& temp = FLUX_API.temp;

    TempSystemDiagnostics diag{};
    if (temp.GetSystemDiagnostics(diag) == TemperatureError::SUCCESS) {
        ESP_LOGI(TAG, "System healthy:      %s", diag.system_healthy ? "YES" : "NO");
        ESP_LOGI(TAG, "Sensors registered:  %lu", static_cast<unsigned long>(diag.total_sensors_registered));
        ESP_LOGI(TAG, "Total operations:    %lu", static_cast<unsigned long>(diag.total_operations));
        ESP_LOGI(TAG, "Successful reads:    %lu", static_cast<unsigned long>(diag.successful_operations));
        ESP_LOGI(TAG, "Failed reads:        %lu", static_cast<unsigned long>(diag.failed_operations));
        ESP_LOGI(TAG, "Min temp:            %.1f C", static_cast<double>(diag.system_min_temp_celsius));
        ESP_LOGI(TAG, "Max temp:            %.1f C", static_cast<double>(diag.system_max_temp_celsius));
        ESP_LOGI(TAG, "Avg temp:            %.1f C", static_cast<double>(diag.system_avg_temp_celsius));
        ESP_LOGI(TAG, "Uptime (ms):         %llu", static_cast<unsigned long long>(diag.system_uptime_ms));
    }

    // Full stats dump
    temp.DumpStatistics();
}

// ---------------------------------------------------------------------------
// app_main -- entry point
// ---------------------------------------------------------------------------
extern "C" void app_main(void) {
    ESP_LOGI(TAG, "Flux TemperatureManager Example");
    ESP_LOGI(TAG, "===============================");

    auto& flux = Flux::GetInstance();
    if (!flux.EnsureInitialized()) {
        ESP_LOGE(TAG, "Flux initialization failed");
    }

    example_basic_reading();
    vTaskDelay(pdMS_TO_TICKS(100));

    example_convenience_methods();
    vTaskDelay(pdMS_TO_TICKS(100));

    example_fahrenheit();
    vTaskDelay(pdMS_TO_TICKS(100));

    example_temperature_stats();
    vTaskDelay(pdMS_TO_TICKS(100));

    example_sensor_info();
    vTaskDelay(pdMS_TO_TICKS(100));

    example_monitoring_loop();
    vTaskDelay(pdMS_TO_TICKS(100));

    example_thermal_protection();
    vTaskDelay(pdMS_TO_TICKS(100));

    example_diagnostics();

    ESP_LOGI(TAG, "===============================");
    ESP_LOGI(TAG, "TemperatureManager example complete");
}
