/**
 * @file GpioManagerExample.cpp
 * @brief Documentation example: GPIO management on Flux V1.
 *
 * Demonstrates the GpioManager API for the Flux valve-control board:
 *   - Pin lookup by name (string-based access)
 *   - Digital output: Set, SetActive, SetInactive, Toggle
 *   - Digital input: Read, IsActive
 *   - Pin configuration: direction, pull mode, output mode
 *   - Registry queries: Contains, Size
 *   - Diagnostics dump
 *
 * Flux V1 GPIO pins (registered as GPIO category):
 *   - "GPIO_MAX22200_ENABLE"  (GPIO2)  -- MAX22200 chip enable
 *   - "GPIO_MAX22200_CMD"     (GPIO39) -- MAX22200 command pin
 *   - "GPIO_MAX22200_FAULT_N" (GPIO42) -- MAX22200 fault, active-low
 *   - "GPIO_MAX22200_TRIGA"   (GPIO40) -- MAX22200 trigger A
 *   - "GPIO_MAX22200_TRIGB"   (GPIO41) -- MAX22200 trigger B
 *   - "GPIO_TLE92466_RESN"    (GPIO6)  -- TLE92466 reset, active-low
 *   - "GPIO_TLE92466_EN"      (GPIO5)  -- TLE92466 enable
 *   - "GPIO_TLE92466_DRV0"    (GPIO7)  -- TLE92466 external drive 0
 *   - "GPIO_TLE92466_DRV1"    (GPIO15) -- TLE92466 external drive 1
 *   - "GPIO_TLE92466_FAULT_N" (GPIO16) -- TLE92466 fault, active-low
 *   - "GPIO_USER_1"           (GPIO20) -- free for application use
 *   - "GPIO_USER_2"           (GPIO23) -- free for application use
 *
 * @note Driver control pins (ENABLE, CMD, RESN, EN) are normally managed by
 *       the ValveManager during initialization. Manually toggling them while
 *       the valve subsystem is active may cause undefined driver behavior.
 *       Prefer using "GPIO_USER_1" and "GPIO_USER_2" for application logic.
 *
 * @author HardFOC Team
 * @date 2025
 * @copyright HardFOC
 */

#include "api/Flux.h"
#include "managers/GpioManager.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "GpioExample";

// ---------------------------------------------------------------------------
// Example 1: Pin registry and lookup
// ---------------------------------------------------------------------------
static void example_pin_lookup() {
    ESP_LOGI(TAG, "=== Example 1: Pin Lookup ===");

    auto& gpio = FLUX_API.gpio;

    // Total registered GPIO pins
    ESP_LOGI(TAG, "Registered GPIO pins: %zu", gpio.Size());

    // Check if specific pins exist
    const char* pins_to_check[] = {
        "GPIO_MAX22200_ENABLE",
        "GPIO_TLE92466_EN",
        "GPIO_USER_1",
        "GPIO_USER_2",
        "GPIO_NONEXISTENT"  // This one should not exist
    };

    for (const char* name : pins_to_check) {
        bool exists = gpio.Contains(name);
        ESP_LOGI(TAG, "  %-25s : %s", name, exists ? "found" : "NOT found");
    }

    // Get the raw BaseGpio driver for advanced operations
    BaseGpio* user_pin = gpio.Get("GPIO_USER_1");
    if (user_pin != nullptr) {
        ESP_LOGI(TAG, "GPIO_USER_1 driver instance: available");
    }
}

// ---------------------------------------------------------------------------
// Example 2: Digital output -- User GPIO
// ---------------------------------------------------------------------------
static void example_digital_output() {
    ESP_LOGI(TAG, "=== Example 2: Digital Output ===");

    auto& gpio = FLUX_API.gpio;

    // Configure USER_GPIO_1 as push-pull output
    hf_gpio_err_t err;

    err = gpio.SetDirection("GPIO_USER_1", hf_gpio_direction_t::HF_GPIO_DIRECTION_OUTPUT);
    ESP_LOGI(TAG, "SetDirection(OUTPUT): %s", (err == hf_gpio_err_t::GPIO_SUCCESS) ? "OK" : "FAIL");

    err = gpio.SetOutputMode("GPIO_USER_1", hf_gpio_output_mode_t::HF_GPIO_OUTPUT_MODE_PUSH_PULL);
    ESP_LOGI(TAG, "SetOutputMode(PUSH_PULL): %s", (err == hf_gpio_err_t::GPIO_SUCCESS) ? "OK" : "FAIL");

    // Set high, then low
    err = gpio.Set("GPIO_USER_1", true);
    ESP_LOGI(TAG, "Set(true): %s", (err == hf_gpio_err_t::GPIO_SUCCESS) ? "OK" : "FAIL");

    vTaskDelay(pdMS_TO_TICKS(10));

    err = gpio.Set("GPIO_USER_1", false);
    ESP_LOGI(TAG, "Set(false): %s", (err == hf_gpio_err_t::GPIO_SUCCESS) ? "OK" : "FAIL");

    // SetActive / SetInactive use logic-level awareness from pin config
    err = gpio.SetActive("GPIO_USER_1");
    ESP_LOGI(TAG, "SetActive: %s", (err == hf_gpio_err_t::GPIO_SUCCESS) ? "OK" : "FAIL");

    err = gpio.SetInactive("GPIO_USER_1");
    ESP_LOGI(TAG, "SetInactive: %s", (err == hf_gpio_err_t::GPIO_SUCCESS) ? "OK" : "FAIL");

    // Toggle the pin
    err = gpio.Toggle("GPIO_USER_1");
    ESP_LOGI(TAG, "Toggle: %s", (err == hf_gpio_err_t::GPIO_SUCCESS) ? "OK" : "FAIL");
}

// ---------------------------------------------------------------------------
// Example 3: Digital input -- Fault monitoring
// ---------------------------------------------------------------------------
static void example_digital_input() {
    ESP_LOGI(TAG, "=== Example 3: Digital Input (Fault Pins) ===");

    auto& gpio = FLUX_API.gpio;

    // Fault pins are active-low inputs with pull-ups (configured in pincfg)
    // Reading them shows the current fault state of the driver ICs

    bool state = false;
    hf_gpio_err_t err;

    err = gpio.Read("GPIO_MAX22200_FAULT_N", state);
    if (err == hf_gpio_err_t::GPIO_SUCCESS) {
        // Pin is logic-inverted: physical LOW = fault active
        ESP_LOGI(TAG, "MAX22200 FAULT_N raw: %s", state ? "HIGH (no fault)" : "LOW (FAULT!)");
    } else {
        ESP_LOGW(TAG, "Could not read MAX22200 fault pin");
    }

    err = gpio.Read("GPIO_TLE92466_FAULT_N", state);
    if (err == hf_gpio_err_t::GPIO_SUCCESS) {
        ESP_LOGI(TAG, "TLE92466 FAULT_N raw: %s", state ? "HIGH (no fault)" : "LOW (FAULT!)");
    }

    // IsActive accounts for logic inversion from pincfg
    bool active = false;
    err = gpio.IsActive("GPIO_MAX22200_FAULT_N", active);
    if (err == hf_gpio_err_t::GPIO_SUCCESS) {
        ESP_LOGI(TAG, "MAX22200 fault active (logic-aware): %s", active ? "YES" : "NO");
    }
}

// ---------------------------------------------------------------------------
// Example 4: Pin configuration
// ---------------------------------------------------------------------------
static void example_pin_configuration() {
    ESP_LOGI(TAG, "=== Example 4: Pin Configuration ===");

    auto& gpio = FLUX_API.gpio;
    hf_gpio_err_t err;

    // Configure USER_GPIO_2 as input with pull-down
    err = gpio.SetDirection("GPIO_USER_2", hf_gpio_direction_t::HF_GPIO_DIRECTION_INPUT);
    ESP_LOGI(TAG, "USER_2 -> INPUT: %s", (err == hf_gpio_err_t::GPIO_SUCCESS) ? "OK" : "FAIL");

    err = gpio.SetPullMode("GPIO_USER_2", hf_gpio_pull_mode_t::HF_GPIO_PULL_MODE_DOWN);
    ESP_LOGI(TAG, "USER_2 -> PULL_DOWN: %s", (err == hf_gpio_err_t::GPIO_SUCCESS) ? "OK" : "FAIL");

    // Read the input state
    bool state = false;
    err = gpio.Read("GPIO_USER_2", state);
    if (err == hf_gpio_err_t::GPIO_SUCCESS) {
        ESP_LOGI(TAG, "USER_2 state: %s", state ? "HIGH" : "LOW");
    }

    // Reconfigure as output with open-drain (useful for shared bus signaling)
    err = gpio.SetDirection("GPIO_USER_2", hf_gpio_direction_t::HF_GPIO_DIRECTION_OUTPUT);
    err = gpio.SetOutputMode("GPIO_USER_2", hf_gpio_output_mode_t::HF_GPIO_OUTPUT_MODE_OPEN_DRAIN);
    ESP_LOGI(TAG, "USER_2 -> OPEN_DRAIN: %s", (err == hf_gpio_err_t::GPIO_SUCCESS) ? "OK" : "FAIL");
}

// ---------------------------------------------------------------------------
// Example 5: GPIO toggle pattern (e.g., heartbeat)
// ---------------------------------------------------------------------------
static void example_heartbeat_pattern() {
    ESP_LOGI(TAG, "=== Example 5: Heartbeat Toggle Pattern ===");

    auto& gpio = FLUX_API.gpio;

    // Ensure USER_1 is configured as output
    gpio.SetDirection("GPIO_USER_1", hf_gpio_direction_t::HF_GPIO_DIRECTION_OUTPUT);
    gpio.SetOutputMode("GPIO_USER_1", hf_gpio_output_mode_t::HF_GPIO_OUTPUT_MODE_PUSH_PULL);

    // Toggle 5 times at 100 ms interval (visible on oscilloscope/logic analyzer)
    for (int i = 0; i < 5; ++i) {
        gpio.Toggle("GPIO_USER_1");
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    // Leave low
    gpio.Set("GPIO_USER_1", false);
    ESP_LOGI(TAG, "Heartbeat pattern complete (5 toggles @ 100ms)");
}

// ---------------------------------------------------------------------------
// Example 6: Statistics dump
// ---------------------------------------------------------------------------
static void example_statistics() {
    ESP_LOGI(TAG, "=== Example 6: GPIO Statistics ===");
    FLUX_API.gpio.DumpStatistics();
}

// ---------------------------------------------------------------------------
// app_main -- entry point
// ---------------------------------------------------------------------------
extern "C" void app_main(void) {
    ESP_LOGI(TAG, "Flux GpioManager Example");
    ESP_LOGI(TAG, "========================");

    auto& flux = Flux::GetInstance();
    if (!flux.EnsureInitialized()) {
        ESP_LOGE(TAG, "Flux initialization failed");
    }

    example_pin_lookup();
    vTaskDelay(pdMS_TO_TICKS(100));

    example_digital_output();
    vTaskDelay(pdMS_TO_TICKS(100));

    example_digital_input();
    vTaskDelay(pdMS_TO_TICKS(100));

    example_pin_configuration();
    vTaskDelay(pdMS_TO_TICKS(100));

    example_heartbeat_pattern();
    vTaskDelay(pdMS_TO_TICKS(100));

    example_statistics();

    ESP_LOGI(TAG, "========================");
    ESP_LOGI(TAG, "GpioManager example complete");
}
