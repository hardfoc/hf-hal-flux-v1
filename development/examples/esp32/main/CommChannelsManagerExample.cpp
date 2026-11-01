/**
 * @file CommChannelsManagerExample.cpp
 * @brief Documentation example: Communication channels usage on Flux V1.
 *
 * Demonstrates the CommChannelsManager API for the Flux valve-control board:
 *   - System initialization via the Flux singleton
 *   - SPI device access (MAX22200 + TLE92466ED)
 *   - I2C and CAN bus access (future expansion)
 *   - Diagnostics snapshot and error reporting
 *   - Proper error handling with CommError
 *
 * Hardware context:
 *   - SPI bus on GPIO36 (SCK), GPIO37 (MOSI), GPIO35 (MISO)
 *   - MAX22200 on CS GPIO38 (SPI Mode 0)
 *   - TLE92466ED on CS GPIO4 (SPI Mode 1)
 *   - I2C on GPIO47 (SDA), GPIO48 (SCL) -- reserved for future use
 *   - CAN/TWAI on GPIO17 (TX), GPIO18 (RX)
 *
 * @author HardFOC Team
 * @date 2025
 * @copyright HardFOC
 */

#include "api/Flux.h"
#include "managers/CommChannelsManager.h"

#include "esp_log.h"
#include "OsUtility.h"
static const char* TAG = "CommExample";

// ---------------------------------------------------------------------------
// Example 1: Basic SPI device access
// ---------------------------------------------------------------------------
static void example_spi_device_access() {
    ESP_LOGI(TAG, "=== Example 1: SPI Device Access ===");

    auto& flux = FLUX_API;
    auto& comms = flux.comms;

    // Access the MAX22200 solenoid driver via enum
    BaseSpi* max22200 = comms.GetSpiDevice(SpiDeviceId::MAX22200_SOLENOID_DRIVER);
    if (max22200 != nullptr) {
        ESP_LOGI(TAG, "MAX22200 SPI device: available");
    } else {
        ESP_LOGW(TAG, "MAX22200 SPI device: not available");
        ESP_LOGW(TAG, "  Last error: %s",
                 CommErrorToString(comms.GetLastError()));
    }

    // Access the TLE92466ED proportional driver via enum
    BaseSpi* tle92466 = comms.GetSpiDevice(SpiDeviceId::TLE92466_LOWSIDE_DRIVER);
    if (tle92466 != nullptr) {
        ESP_LOGI(TAG, "TLE92466ED SPI device: available");
    } else {
        ESP_LOGW(TAG, "TLE92466ED SPI device: not available");
    }

    // Access by raw index (equivalent to enum-based access)
    BaseSpi* device_0 = comms.GetSpiDevice(static_cast<uint8_t>(0));
    BaseSpi* device_1 = comms.GetSpiDevice(static_cast<uint8_t>(1));
    ESP_LOGI(TAG, "Device[0] (MAX22200): %s", device_0 ? "OK" : "NULL");
    ESP_LOGI(TAG, "Device[1] (TLE92466): %s", device_1 ? "OK" : "NULL");
}

// ---------------------------------------------------------------------------
// Example 2: I2C and CAN bus access
// ---------------------------------------------------------------------------
static void example_bus_access() {
    ESP_LOGI(TAG, "=== Example 2: I2C and CAN Bus Access ===");

    auto& comms = FLUX_API.comms;

    // I2C bus -- reserved for future expansion on Flux
    EspI2cBus* i2c = comms.GetI2cBus();
    ESP_LOGI(TAG, "I2C bus: %s", i2c ? "available" : "not configured");

    // CAN/TWAI bus -- available for valve network communication
    BaseCan* can = comms.GetCanBus();
    ESP_LOGI(TAG, "CAN bus: %s", can ? "available" : "not configured");
}

// ---------------------------------------------------------------------------
// Example 3: System diagnostics
// ---------------------------------------------------------------------------
static void example_diagnostics() {
    ESP_LOGI(TAG, "=== Example 3: Comm Diagnostics ===");

    auto& comms = FLUX_API.comms;

    CommSystemDiagnostics diag{};
    CommError err = comms.GetSystemDiagnostics(diag);

    if (err == CommError::SUCCESS) {
        ESP_LOGI(TAG, "System healthy:    %s", diag.system_healthy ? "YES" : "NO");
        ESP_LOGI(TAG, "Initialized:       %s", diag.system_initialized ? "YES" : "NO");
        ESP_LOGI(TAG, "SPI bus valid:     %s", diag.spi_bus_valid ? "YES" : "NO");
        ESP_LOGI(TAG, "I2C bus valid:     %s", diag.i2c_bus_valid ? "YES" : "NO");
        ESP_LOGI(TAG, "CAN bus valid:     %s", diag.can_bus_valid ? "YES" : "NO");
        ESP_LOGI(TAG, "SPI device count:  %u", diag.spi_device_count);
        ESP_LOGI(TAG, "Last error:        %s", CommErrorToString(diag.last_error));
    } else {
        ESP_LOGE(TAG, "Failed to get diagnostics: %s", CommErrorToString(err));
    }
}

// ---------------------------------------------------------------------------
// Example 4: Error handling patterns
// ---------------------------------------------------------------------------
static void example_error_handling() {
    ESP_LOGI(TAG, "=== Example 4: Error Handling ===");

    auto& comms = FLUX_API.comms;

    // CommChannelsManager is the only Flux manager with a public GetLastError()
    CommError last = comms.GetLastError();
    ESP_LOGI(TAG, "Current error state: %s (code %u)",
             CommErrorToString(last), static_cast<unsigned>(last));

    // Accessing an invalid device index returns nullptr
    BaseSpi* invalid = comms.GetSpiDevice(static_cast<uint8_t>(99));
    if (invalid == nullptr) {
        CommError after = comms.GetLastError();
        ESP_LOGI(TAG, "Invalid device access -> error: %s",
                 CommErrorToString(after));
    }

    // Checking initialization state before operations
    if (!comms.IsInitialized()) {
        ESP_LOGW(TAG, "Comms not initialized -- call EnsureInitialized() first");
    }
}

// ---------------------------------------------------------------------------
// Example 5: Full statistics dump
// ---------------------------------------------------------------------------
static void example_statistics_dump() {
    ESP_LOGI(TAG, "=== Example 5: Statistics Dump ===");

    // DumpStatistics() prints a formatted summary to the logger
    FLUX_API.comms.DumpStatistics();
}

// ---------------------------------------------------------------------------
// app_main -- entry point
// ---------------------------------------------------------------------------
extern "C" void app_main(void) {
    ESP_LOGI(TAG, "Flux CommChannelsManager Example");
    ESP_LOGI(TAG, "================================");

    // Step 1: Initialize the entire Flux system (CommChannels is initialized first)
    auto& flux = Flux::GetInstance();
    if (!flux.EnsureInitialized()) {
        ESP_LOGE(TAG, "Flux initialization failed -- comms may be partially available");
    }

    // Step 2: Run examples
    example_spi_device_access();
    os_delay_msec(100);

    example_bus_access();
    os_delay_msec(100);

    example_diagnostics();
    os_delay_msec(100);

    example_error_handling();
    os_delay_msec(100);

    example_statistics_dump();

    ESP_LOGI(TAG, "================================");
    ESP_LOGI(TAG, "CommChannelsManager example complete");
}
