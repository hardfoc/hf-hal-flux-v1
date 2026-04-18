/**
 * @file AdcManagerExample.cpp
 * @brief Documentation example: ADC reading on Flux V1.
 *
 * Demonstrates the AdcManager API for the Flux valve-control board:
 *   - Channel lookup and registry queries
 *   - Voltage reading (auto-scaled through voltage divider)
 *   - Raw ADC count reading
 *   - Multi-sample averaging for noise reduction
 *   - VBUS voltage monitoring (11x divider)
 *   - NTC thermistor raw voltage reading (temperature via TemperatureManager)
 *   - Diagnostics snapshot
 *
 * Flux V1 ADC channels:
 *   - "ADC_NTC_THERMISTOR"  (ADC1_CH4, GPIO4, divider=1.0)
 *   - "ADC_VBUS_MONITOR"    (ADC1_CH0, GPIO0, divider=11.0)
 *
 * @note For temperature readings from the NTC thermistor, prefer using
 *       TemperatureManager which handles the NTC resistance-to-temperature
 *       conversion. Use AdcManager directly for raw voltage/count access.
 *
 * @author HardFOC Team
 * @date 2025
 * @copyright HardFOC
 */

#include "api/Flux.h"
#include "managers/AdcManager.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "AdcExample";

// ---------------------------------------------------------------------------
// Example 1: Channel registry
// ---------------------------------------------------------------------------
static void example_channel_registry() {
    ESP_LOGI(TAG, "=== Example 1: Channel Registry ===");

    auto& adc = FLUX_API.adc;

    ESP_LOGI(TAG, "Registered ADC channels: %zu", adc.Size());

    // Check channel availability
    ESP_LOGI(TAG, "  ADC_NTC_THERMISTOR: %s", adc.Contains("ADC_NTC_THERMISTOR") ? "found" : "NOT found");
    ESP_LOGI(TAG, "  ADC_VBUS_MONITOR:   %s", adc.Contains("ADC_VBUS_MONITOR") ? "found" : "NOT found");
    ESP_LOGI(TAG, "  ADC_NONEXISTENT:    %s", adc.Contains("ADC_NONEXISTENT") ? "found" : "NOT found");

    // Get the raw BaseAdc driver (for advanced operations)
    BaseAdc* ntc_driver = adc.Get("ADC_NTC_THERMISTOR");
    ESP_LOGI(TAG, "  NTC driver instance: %s", ntc_driver ? "available" : "NULL");
}

// ---------------------------------------------------------------------------
// Example 2: VBUS voltage monitoring
// ---------------------------------------------------------------------------
static void example_vbus_monitoring() {
    ESP_LOGI(TAG, "=== Example 2: VBUS Voltage Monitoring ===");

    auto& adc = FLUX_API.adc;

    // ReadVoltage auto-applies the 11x voltage divider from pincfg,
    // so the returned value is the actual bus voltage.

    float vbus = 0.0f;
    hf_adc_err_t err = adc.ReadVoltage("ADC_VBUS_MONITOR", vbus);

    if (err == hf_adc_err_t::ADC_SUCCESS) {
        ESP_LOGI(TAG, "VBUS voltage: %.2f V", static_cast<double>(vbus));

        // Typical ranges:
        //   12V system: expect ~11.5-12.5 V
        //   24V system: expect ~23.0-25.0 V
        if (vbus < 10.0f) {
            ESP_LOGW(TAG, "  WARNING: VBUS below 10V -- check power supply");
        } else if (vbus > 28.0f) {
            ESP_LOGW(TAG, "  WARNING: VBUS above 28V -- overvoltage condition");
        } else {
            ESP_LOGI(TAG, "  VBUS within normal range");
        }
    } else {
        ESP_LOGE(TAG, "VBUS read failed (error %d)", static_cast<int>(err));
    }
}

// ---------------------------------------------------------------------------
// Example 3: NTC thermistor raw voltage
// ---------------------------------------------------------------------------
static void example_ntc_raw_voltage() {
    ESP_LOGI(TAG, "=== Example 3: NTC Thermistor Raw Voltage ===");

    auto& adc = FLUX_API.adc;

    // Read the raw voltage across the NTC thermistor voltage divider.
    // For actual temperature, use TemperatureManager.ReadNtcTemperature().
    float voltage = 0.0f;
    hf_adc_err_t err = adc.ReadVoltage("ADC_NTC_THERMISTOR", voltage);

    if (err == hf_adc_err_t::ADC_SUCCESS) {
        ESP_LOGI(TAG, "NTC voltage: %.3f V", static_cast<double>(voltage));
    }

    // Also read the raw ADC count for debugging
    uint32_t raw = 0;
    err = adc.ReadRaw("ADC_NTC_THERMISTOR", raw);

    if (err == hf_adc_err_t::ADC_SUCCESS) {
        ESP_LOGI(TAG, "NTC raw count: %lu (12-bit: 0-4095)", static_cast<unsigned long>(raw));
    }
}

// ---------------------------------------------------------------------------
// Example 4: Multi-sample averaging
// ---------------------------------------------------------------------------
static void example_multi_sample() {
    ESP_LOGI(TAG, "=== Example 4: Multi-Sample Averaging ===");

    auto& adc = FLUX_API.adc;

    // Single sample vs. averaged readings.
    // More samples reduce noise but take longer.

    float v1 = 0.0f, v8 = 0.0f, v32 = 0.0f;

    adc.ReadVoltage("ADC_VBUS_MONITOR", v1, 1);    // 1 sample
    adc.ReadVoltage("ADC_VBUS_MONITOR", v8, 8);    // 8 samples averaged
    adc.ReadVoltage("ADC_VBUS_MONITOR", v32, 32);  // 32 samples averaged

    ESP_LOGI(TAG, "VBUS (1 sample):   %.3f V", static_cast<double>(v1));
    ESP_LOGI(TAG, "VBUS (8 samples):  %.3f V", static_cast<double>(v8));
    ESP_LOGI(TAG, "VBUS (32 samples): %.3f V", static_cast<double>(v32));

    // Raw count with averaging
    uint32_t raw1 = 0, raw16 = 0;
    adc.ReadRaw("ADC_NTC_THERMISTOR", raw1, 1);
    adc.ReadRaw("ADC_NTC_THERMISTOR", raw16, 16);
    ESP_LOGI(TAG, "NTC raw (1 sample):  %lu", static_cast<unsigned long>(raw1));
    ESP_LOGI(TAG, "NTC raw (16 samples): %lu", static_cast<unsigned long>(raw16));
}

// ---------------------------------------------------------------------------
// Example 5: Periodic VBUS monitoring loop
// ---------------------------------------------------------------------------
static void example_monitoring_loop() {
    ESP_LOGI(TAG, "=== Example 5: Periodic VBUS Monitor (5 readings) ===");

    auto& adc = FLUX_API.adc;

    for (int i = 0; i < 5; ++i) {
        float vbus = 0.0f;
        hf_adc_err_t err = adc.ReadVoltage("ADC_VBUS_MONITOR", vbus, 8);

        if (err == hf_adc_err_t::ADC_SUCCESS) {
            ESP_LOGI(TAG, "  [%d] VBUS: %.2f V", i, static_cast<double>(vbus));
        } else {
            ESP_LOGW(TAG, "  [%d] VBUS read error: %d", i, static_cast<int>(err));
        }

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

// ---------------------------------------------------------------------------
// Example 6: Error handling
// ---------------------------------------------------------------------------
static void example_error_handling() {
    ESP_LOGI(TAG, "=== Example 6: Error Handling ===");

    auto& adc = FLUX_API.adc;

    // Reading a nonexistent channel returns an error
    float voltage = 0.0f;
    hf_adc_err_t err = adc.ReadVoltage("ADC_NONEXISTENT", voltage);
    ESP_LOGI(TAG, "Read nonexistent channel: error=%d (expected non-zero)", static_cast<int>(err));

    // Getting a driver for a nonexistent channel returns nullptr
    BaseAdc* bad = adc.Get("DOES_NOT_EXIST");
    ESP_LOGI(TAG, "Get nonexistent: %s (expected NULL)", bad ? "NOT NULL" : "NULL");
}

// ---------------------------------------------------------------------------
// Example 7: Diagnostics
// ---------------------------------------------------------------------------
static void example_diagnostics() {
    ESP_LOGI(TAG, "=== Example 7: ADC Diagnostics ===");

    auto& adc = FLUX_API.adc;

    AdcSystemDiagnostics diag{};
    if (adc.GetSystemDiagnostics(diag) == hf_adc_err_t::ADC_SUCCESS) {
        ESP_LOGI(TAG, "System healthy:      %s", diag.system_healthy ? "YES" : "NO");
        ESP_LOGI(TAG, "Total channels:      %u", diag.total_channels);
        ESP_LOGI(TAG, "Registered channels: %u", diag.registered_channels);
        ESP_LOGI(TAG, "Total reads:         %lu", static_cast<unsigned long>(diag.total_reads));
        ESP_LOGI(TAG, "Total errors:        %lu", static_cast<unsigned long>(diag.total_errors));
    }

    // Full statistics dump
    adc.DumpStatistics();
}

// ---------------------------------------------------------------------------
// app_main -- entry point
// ---------------------------------------------------------------------------
extern "C" void app_main(void) {
    ESP_LOGI(TAG, "Flux AdcManager Example");
    ESP_LOGI(TAG, "=======================");

    auto& flux = Flux::GetInstance();
    if (!flux.EnsureInitialized()) {
        ESP_LOGE(TAG, "Flux initialization failed");
    }

    example_channel_registry();
    vTaskDelay(pdMS_TO_TICKS(100));

    example_vbus_monitoring();
    vTaskDelay(pdMS_TO_TICKS(100));

    example_ntc_raw_voltage();
    vTaskDelay(pdMS_TO_TICKS(100));

    example_multi_sample();
    vTaskDelay(pdMS_TO_TICKS(100));

    example_monitoring_loop();
    vTaskDelay(pdMS_TO_TICKS(100));

    example_error_handling();
    vTaskDelay(pdMS_TO_TICKS(100));

    example_diagnostics();

    ESP_LOGI(TAG, "=======================");
    ESP_LOGI(TAG, "AdcManager example complete");
}
