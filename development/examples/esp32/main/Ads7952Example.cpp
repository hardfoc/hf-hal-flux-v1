/**
 * @file Ads7952Example.cpp
 * @brief Documentation example: ADS7952 12-channel ADC on Flux V1.
 *
 * Demonstrates the ADS7952 external ADC chip integrated into the Flux platform:
 *   - Reading individual ADS7952 channels via AdcManager (by name)
 *   - Reading raw counts vs. scaled voltage
 *   - Multi-sample averaging for noise reduction
 *   - All-channel scan using ADS7952-specific ReadAllChannels()
 *   - Input range switching (Vref / 2*Vref)
 *   - Alarm threshold programming (voltage and raw count)
 *   - Handler-level diagnostics
 *
 * Flux V1 ADS7952 hardware:
 *   - TI ADS7952: 12-channel, 12-bit SAR ADC
 *   - SPI3 bus: GPIO10 (nCS), GPIO11 (MOSI), GPIO12 (SCK), GPIO13 (MISO)
 *   - External Vref: 2.5 V, VA supply: up to 5.0 V
 *   - SPI clock: 20 MHz, Mode 0 (CPOL=0, CPHA=0)
 *
 * Registered channel names (via pin config):
 *   - "ADS7952_0_CH0"  through  "ADS7952_0_CH11"
 *
 * @note The ADS7952 channels are registered into AdcManager alongside the
 *       ESP32 internal ADC channels. All standard AdcManager operations
 *       (ReadVoltage, ReadRaw) work transparently. For chip-specific
 *       features (alarm programming, range control, full-scan), access
 *       the Ads7952Handler directly.
 *
 * @author HardFOC Team
 * @date 2025
 * @copyright HardFOC
 */

#include "api/Flux.h"
#include "managers/AdcManager.h"
#include "OsUtility.h"

#ifdef HARDFOC_ADS7952_SUPPORT
#include "Ads7952Handler.h"
#endif

#include "esp_log.h"
static const char* TAG = "Ads7952Example";

// ---------------------------------------------------------------------------
// Example 1: Channel availability check
// ---------------------------------------------------------------------------
static void example_channel_registry() {
    ESP_LOGI(TAG, "=== Example 1: ADS7952 Channel Registry ===");

    auto& adc = FLUX_API.adc;

    ESP_LOGI(TAG, "Total registered ADC channels: %zu", adc.Size());

    // Check that ADS7952 channels are registered
    const char* channels[] = {
        "ADS7952_0_CH0",  "ADS7952_0_CH1",  "ADS7952_0_CH2",
        "ADS7952_0_CH3",  "ADS7952_0_CH4",  "ADS7952_0_CH5",
        "ADS7952_0_CH6",  "ADS7952_0_CH7",  "ADS7952_0_CH8",
        "ADS7952_0_CH9",  "ADS7952_0_CH10", "ADS7952_0_CH11",
    };

    int found = 0;
    for (const auto* name : channels) {
        bool present = adc.Contains(name);
        ESP_LOGI(TAG, "  %-18s %s", name, present ? "OK" : "MISSING");
        if (present) ++found;
    }

    ESP_LOGI(TAG, "ADS7952 channels: %d / 12 registered", found);

    // Verify the driver instance behind a channel
    BaseAdc* drv = adc.Get("ADS7952_0_CH0");
    ESP_LOGI(TAG, "  CH0 BaseAdc driver: %s", drv ? "available" : "NULL");
}

// ---------------------------------------------------------------------------
// Example 2: Single-channel voltage reading
// ---------------------------------------------------------------------------
static void example_single_channel_read() {
    ESP_LOGI(TAG, "=== Example 2: Single-Channel Voltage Reading ===");

    auto& adc = FLUX_API.adc;

    // Read voltage on CH0 (uses AdcManager's standard API)
    float voltage = 0.0f;
    hf_adc_err_t err = adc.ReadVoltage("ADS7952_0_CH0", voltage);

    if (err == hf_adc_err_t::ADC_SUCCESS) {
        ESP_LOGI(TAG, "CH0 voltage: %.4f V (Vref=2.5V, 12-bit)",
                 static_cast<double>(voltage));
    } else {
        ESP_LOGE(TAG, "CH0 voltage read failed (error %d)", static_cast<int>(err));
    }

    // Also read the raw 12-bit count
    uint32_t raw = 0;
    err = adc.ReadRaw("ADS7952_0_CH0", raw);

    if (err == hf_adc_err_t::ADC_SUCCESS) {
        ESP_LOGI(TAG, "CH0 raw count: %lu (0-4095)", static_cast<unsigned long>(raw));
    }
}

// ---------------------------------------------------------------------------
// Example 3: Scan all 12 channels via AdcManager
// ---------------------------------------------------------------------------
static void example_scan_all_channels_manager() {
    ESP_LOGI(TAG, "=== Example 3: Scan All 12 Channels (via AdcManager) ===");

    auto& adc = FLUX_API.adc;

    for (int ch = 0; ch < 12; ++ch) {
        char name[20];
        snprintf(name, sizeof(name), "ADS7952_0_CH%d", ch);

        float voltage = 0.0f;
        hf_adc_err_t err = adc.ReadVoltage(name, voltage);

        if (err == hf_adc_err_t::ADC_SUCCESS) {
            ESP_LOGI(TAG, "  CH%-2d: %.4f V", ch, static_cast<double>(voltage));
        } else {
            ESP_LOGW(TAG, "  CH%-2d: ERROR %d", ch, static_cast<int>(err));
        }
    }
}

// ---------------------------------------------------------------------------
// Example 4: Multi-sample averaging
// ---------------------------------------------------------------------------
static void example_multi_sample() {
    ESP_LOGI(TAG, "=== Example 4: Multi-Sample Averaging ===");

    auto& adc = FLUX_API.adc;

    // Compare single-sample vs. averaged readings on CH0
    float v1 = 0.0f, v8 = 0.0f, v32 = 0.0f;

    adc.ReadVoltage("ADS7952_0_CH0", v1, 1);    // 1 sample
    adc.ReadVoltage("ADS7952_0_CH0", v8, 8);    // 8 samples averaged
    adc.ReadVoltage("ADS7952_0_CH0", v32, 32);  // 32 samples averaged

    ESP_LOGI(TAG, "CH0 (1 sample):   %.4f V", static_cast<double>(v1));
    ESP_LOGI(TAG, "CH0 (8 samples):  %.4f V", static_cast<double>(v8));
    ESP_LOGI(TAG, "CH0 (32 samples): %.4f V", static_cast<double>(v32));

    // Raw count averaging
    uint32_t raw1 = 0, raw16 = 0;
    adc.ReadRaw("ADS7952_0_CH0", raw1, 1);
    adc.ReadRaw("ADS7952_0_CH0", raw16, 16);
    ESP_LOGI(TAG, "CH0 raw (1 sample):  %lu", static_cast<unsigned long>(raw1));
    ESP_LOGI(TAG, "CH0 raw (16 samples): %lu", static_cast<unsigned long>(raw16));
}

#ifdef HARDFOC_ADS7952_SUPPORT
// ---------------------------------------------------------------------------
// Example 5: ReadAllChannels (ADS7952-specific, Auto-1 mode)
// ---------------------------------------------------------------------------
static void example_read_all_channels() {
    ESP_LOGI(TAG, "=== Example 5: ReadAllChannels (ADS7952 Auto-1 Scan) ===");

    auto& adc = FLUX_API.adc;

    // Get the BaseAdc* for CH0, then cast to Ads7952Handler.
    // All 12 channels on device 0 share the same handler instance.
    BaseAdc* base = adc.Get("ADS7952_0_CH0");
    auto* handler = static_cast<Ads7952Handler*>(base);

    if (!handler) {
        ESP_LOGE(TAG, "ADS7952 handler not available");
        return;
    }

    ads7952::ChannelReadings readings{};
    if (handler->ReadAllChannels(readings)) {
        ESP_LOGI(TAG, "All-channel scan (valid_mask=0x%03X):",
                 readings.valid_mask);

        for (uint8_t ch = 0; ch < ads7952::ChannelReadings::MAX_CHANNELS; ++ch) {
            if (readings.hasChannel(ch)) {
                ESP_LOGI(TAG, "  CH%-2u: count=%4u  voltage=%.4f V",
                         ch, readings.count[ch],
                         static_cast<double>(readings.voltage[ch]));
            }
        }
    } else {
        ESP_LOGE(TAG, "ReadAllChannels failed (error %d)",
                 static_cast<int>(readings.error));
    }
}

// ---------------------------------------------------------------------------
// Example 6: Range switching (Vref vs. 2*Vref)
// ---------------------------------------------------------------------------
static void example_range_switching() {
    ESP_LOGI(TAG, "=== Example 6: Input Range Switching ===");

    auto& adc = FLUX_API.adc;

    BaseAdc* base = adc.Get("ADS7952_0_CH0");
    auto* handler = static_cast<Ads7952Handler*>(base);

    if (!handler) {
        ESP_LOGE(TAG, "ADS7952 handler not available");
        return;
    }

    // Current range — default is Vref (2.5 V full scale)
    float active_vref = handler->GetActiveVref();
    ESP_LOGI(TAG, "Current active Vref: %.2f V", static_cast<double>(active_vref));

    // Read CH0 in Vref range
    float v_narrow = 0.0f;
    adc.ReadVoltage("ADS7952_0_CH0", v_narrow, 4);
    ESP_LOGI(TAG, "CH0 @ Vref range:   %.4f V", static_cast<double>(v_narrow));

    // Switch to 2*Vref range (5.0V full-scale, clamped to VA)
    if (handler->SetRange(ads7952::Range::TwoVref)) {
        active_vref = handler->GetActiveVref();
        ESP_LOGI(TAG, "Switched to 2*Vref — active Vref: %.2f V",
                 static_cast<double>(active_vref));

        float v_wide = 0.0f;
        adc.ReadVoltage("ADS7952_0_CH0", v_wide, 4);
        ESP_LOGI(TAG, "CH0 @ 2*Vref range: %.4f V", static_cast<double>(v_wide));
    } else {
        ESP_LOGE(TAG, "Range switch failed");
    }

    // Restore original Vref range
    handler->SetRange(ads7952::Range::Vref);
    ESP_LOGI(TAG, "Restored Vref range");
}

// ---------------------------------------------------------------------------
// Example 7: Alarm threshold programming
// ---------------------------------------------------------------------------
static void example_alarm_programming() {
    ESP_LOGI(TAG, "=== Example 7: Alarm Threshold Programming ===");

    auto& adc = FLUX_API.adc;

    BaseAdc* base = adc.Get("ADS7952_0_CH0");
    auto* handler = static_cast<Ads7952Handler*>(base);

    if (!handler) {
        ESP_LOGE(TAG, "ADS7952 handler not available");
        return;
    }

    // Program voltage-based alarms on CH0
    // Low alarm at 0.5 V, high alarm at 2.0 V
    bool ok = handler->ProgramAlarmVoltage(0, ads7952::AlarmBound::Low, 0.5f);
    ESP_LOGI(TAG, "CH0 low  alarm @ 0.5V: %s", ok ? "OK" : "FAILED");

    ok = handler->ProgramAlarmVoltage(0, ads7952::AlarmBound::High, 2.0f);
    ESP_LOGI(TAG, "CH0 high alarm @ 2.0V: %s", ok ? "OK" : "FAILED");

    // Program raw-count alarms on CH4
    // Low alarm at count 500 (~0.305 V), high alarm at count 3500 (~2.136 V)
    ok = handler->ProgramAlarm(4, ads7952::AlarmBound::Low, 500);
    ESP_LOGI(TAG, "CH4 low  alarm @ count 500:  %s", ok ? "OK" : "FAILED");

    ok = handler->ProgramAlarm(4, ads7952::AlarmBound::High, 3500);
    ESP_LOGI(TAG, "CH4 high alarm @ count 3500: %s", ok ? "OK" : "FAILED");

    ESP_LOGI(TAG, "Alarm thresholds programmed");
    ESP_LOGI(TAG, "  Note: Route ADS7952 GPIO pins to respond to alarm events");
}

// ---------------------------------------------------------------------------
// Example 8: visitDriver for safe direct access
// ---------------------------------------------------------------------------
static void example_visit_driver() {
    ESP_LOGI(TAG, "=== Example 8: visitDriver (Mutex-Protected Access) ===");

    auto& adc = FLUX_API.adc;

    BaseAdc* base = adc.Get("ADS7952_0_CH0");
    auto* handler = static_cast<Ads7952Handler*>(base);

    if (!handler) {
        ESP_LOGE(TAG, "ADS7952 handler not available");
        return;
    }

    // Use visitDriver for thread-safe access to the raw ADS7952 driver.
    // The lambda executes under the handler's mutex lock.
    bool initialized = handler->visitDriver([](auto& driver) -> bool {
        return driver.IsInitialized();
    });
    ESP_LOGI(TAG, "Driver initialized: %s", initialized ? "yes" : "no");

    // Read a specific channel through the raw driver under mutex
    float voltage = handler->visitDriver([](auto& driver) -> float {
        auto result = driver.ReadChannel(5);
        return result.ok() ? result.voltage : -1.0f;
    });
    ESP_LOGI(TAG, "CH5 via visitDriver: %.4f V", static_cast<double>(voltage));
}

// ---------------------------------------------------------------------------
// Example 9: Handler diagnostics
// ---------------------------------------------------------------------------
static void example_diagnostics() {
    ESP_LOGI(TAG, "=== Example 9: ADS7952 Handler Diagnostics ===");

    auto& adc = FLUX_API.adc;

    BaseAdc* base = adc.Get("ADS7952_0_CH0");
    auto* handler = static_cast<Ads7952Handler*>(base);

    if (!handler) {
        ESP_LOGE(TAG, "ADS7952 handler not available");
        return;
    }

    // Get structured diagnostics
    Ads7952Diagnostics diag{};
    if (handler->GetHandlerDiagnostics(diag)) {
        ESP_LOGI(TAG, "Device index:   %u", diag.device_index);
        ESP_LOGI(TAG, "Initialized:    %s", diag.initialized ? "yes" : "no");
        ESP_LOGI(TAG, "Vref:           %.2f V", static_cast<double>(diag.vref));
        ESP_LOGI(TAG, "Active Vref:    %.2f V", static_cast<double>(diag.active_vref));
        ESP_LOGI(TAG, "Total reads:    %lu", static_cast<unsigned long>(diag.total_reads));
        ESP_LOGI(TAG, "Error count:    %lu", static_cast<unsigned long>(diag.error_count));
    }

    // Full diagnostics dump to log
    ESP_LOGI(TAG, "--- Full diagnostics dump ---");
    handler->DumpDiagnostics();
}
#endif // HARDFOC_ADS7952_SUPPORT

// ---------------------------------------------------------------------------
// Example 10: Periodic monitoring loop
// ---------------------------------------------------------------------------
static void example_monitoring_loop() {
    ESP_LOGI(TAG, "=== Example 10: Periodic ADC Monitor (5 iterations) ===");

    auto& adc = FLUX_API.adc;

    for (int i = 0; i < 5; ++i) {
        float ch0_v = 0.0f, ch1_v = 0.0f;
        hf_adc_err_t e0 = adc.ReadVoltage("ADS7952_0_CH0", ch0_v, 4);
        hf_adc_err_t e1 = adc.ReadVoltage("ADS7952_0_CH1", ch1_v, 4);

        ESP_LOGI(TAG, "  [%d] CH0: %.4f V %s  CH1: %.4f V %s",
                 i,
                 static_cast<double>(ch0_v),
                 e0 == hf_adc_err_t::ADC_SUCCESS ? "" : "(ERR)",
                 static_cast<double>(ch1_v),
                 e1 == hf_adc_err_t::ADC_SUCCESS ? "" : "(ERR)");

        os_delay_msec(250);
    }
}

// ---------------------------------------------------------------------------
// app_main -- entry point
// ---------------------------------------------------------------------------
extern "C" void app_main(void) {
    ESP_LOGI(TAG, "Flux ADS7952 ADC Example");
    ESP_LOGI(TAG, "========================");
    ESP_LOGI(TAG, "Hardware: ADS7952 12-ch 12-bit SAR ADC");
    ESP_LOGI(TAG, "SPI3 bus: CS=GPIO10, MOSI=GPIO11, SCK=GPIO12, MISO=GPIO13");
    ESP_LOGI(TAG, "Vref=2.5V, VA=5.0V");

    auto& flux = Flux::GetInstance();
    if (!flux.EnsureInitialized()) {
        ESP_LOGE(TAG, "Flux initialization failed — aborting");
        return;
    }

    // Standard AdcManager examples (work with any ADC backend)
    example_channel_registry();
    os_delay_msec(100);

    example_single_channel_read();
    os_delay_msec(100);

    example_scan_all_channels_manager();
    os_delay_msec(100);

    example_multi_sample();
    os_delay_msec(100);

#ifdef HARDFOC_ADS7952_SUPPORT
    // ADS7952-specific examples (require direct handler access)
    example_read_all_channels();
    os_delay_msec(100);

    example_range_switching();
    os_delay_msec(100);

    example_alarm_programming();
    os_delay_msec(100);

    example_visit_driver();
    os_delay_msec(100);

    example_diagnostics();
    os_delay_msec(100);
#endif

    // Periodic monitoring (uses standard AdcManager)
    example_monitoring_loop();

    ESP_LOGI(TAG, "========================");
    ESP_LOGI(TAG, "ADS7952 example complete");
}
