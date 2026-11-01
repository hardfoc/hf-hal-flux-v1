/**
 * @file LedManagerExample.cpp
 * @brief Documentation example: WS2812 LED control on Flux V1.
 *
 * Demonstrates the LedManager API for the Flux valve-control board:
 *   - Basic color setting (RGB struct and raw uint32_t)
 *   - Predefined color constants (LedColors namespace)
 *   - Brightness control (percent and raw 0-255)
 *   - Animation control: solid, blink, breath, rainbow
 *   - Status indicators: boot, ready, warning, error, calibration
 *   - Animation lifecycle: start, update, stop
 *   - Color utility: color wheel for rainbow generation
 *   - Diagnostics snapshot
 *
 * Hardware context:
 *   - Single WS2812 RGB LED on GPIO3 (via RMT peripheral)
 *   - Same hardware as Vortex -- used for system status indication
 *
 * @author HardFOC Team
 * @date 2025
 * @copyright HardFOC
 */

#include "api/Flux.h"
#include "managers/LedManager.h"

#include "esp_log.h"
#include "OsUtility.h"
static const char* TAG = "LedExample";

/// Helper: log an LED error result
static void log_led_result(const char* operation, LedError err) {
    if (err == LedError::SUCCESS) {
        ESP_LOGI(TAG, "  %s: OK", operation);
    } else {
        ESP_LOGW(TAG, "  %s: %s", operation, LedErrorToString(err));
    }
}

// ---------------------------------------------------------------------------
// Example 1: Basic color setting
// ---------------------------------------------------------------------------
static void example_basic_colors() {
    ESP_LOGI(TAG, "=== Example 1: Basic Colors ===");

    auto& leds = FLUX_API.leds;
    LedError err;

    // Set color using LedColor struct
    err = leds.SetColor(LedColor(255, 0, 0));  // Red
    log_led_result("SetColor(255, 0, 0) [Red]", err);
    os_delay_msec(500);

    // Set color using predefined constants
    err = leds.SetColor(LedColors::GREEN);
    log_led_result("SetColor(GREEN)", err);
    os_delay_msec(500);

    err = leds.SetColor(LedColors::BLUE);
    log_led_result("SetColor(BLUE)", err);
    os_delay_msec(500);

    // Set color using raw RGB uint32_t (0xRRGGBB)
    err = leds.SetColor(static_cast<uint32_t>(0xFF8C00));  // Dark orange
    log_led_result("SetColor(0xFF8C00) [DarkOrange]", err);
    os_delay_msec(500);

    // Read back the current color
    LedColor current{};
    err = leds.GetCurrentColor(current);
    if (err == LedError::SUCCESS) {
        ESP_LOGI(TAG, "  Current color: R=%u G=%u B=%u (0x%06lX)",
                 current.red, current.green, current.blue,
                 static_cast<unsigned long>(current.ToRgb()));
    }

    // Turn off
    err = leds.TurnOff();
    log_led_result("TurnOff()", err);
}

// ---------------------------------------------------------------------------
// Example 2: Predefined color palette
// ---------------------------------------------------------------------------
static void example_color_palette() {
    ESP_LOGI(TAG, "=== Example 2: Color Palette ===");

    auto& leds = FLUX_API.leds;

    // Cycle through all predefined colors
    struct { const char* name; LedColor color; } palette[] = {
        {"RED",     LedColors::RED},
        {"GREEN",   LedColors::GREEN},
        {"BLUE",    LedColors::BLUE},
        {"YELLOW",  LedColors::YELLOW},
        {"CYAN",    LedColors::CYAN},
        {"MAGENTA", LedColors::MAGENTA},
        {"WHITE",   LedColors::WHITE},
        {"ORANGE",  LedColors::ORANGE},
        {"PURPLE",  LedColors::PURPLE},
    };

    for (const auto& entry : palette) {
        leds.SetColor(entry.color);
        ESP_LOGI(TAG, "  %s (R%u G%u B%u)", entry.name,
                 entry.color.red, entry.color.green, entry.color.blue);
        os_delay_msec(300);
    }

    leds.TurnOff();
}

// ---------------------------------------------------------------------------
// Example 3: Brightness control
// ---------------------------------------------------------------------------
static void example_brightness() {
    ESP_LOGI(TAG, "=== Example 3: Brightness Control ===");

    auto& leds = FLUX_API.leds;

    // Set a base color
    leds.SetColor(LedColors::WHITE);

    // Brightness by percentage (0-100)
    const uint8_t levels[] = {10, 25, 50, 75, 100};
    for (uint8_t pct : levels) {
        LedError err = leds.SetBrightnessPercent(pct);
        ESP_LOGI(TAG, "  Brightness %u%%: %s", pct,
                 (err == LedError::SUCCESS) ? "OK" : LedErrorToString(err));
        os_delay_msec(400);
    }

    // Brightness by raw value (0-255)
    leds.SetBrightnessRaw(128);
    ESP_LOGI(TAG, "  BrightnessRaw(128): 50%% of max range");
    os_delay_msec(400);

    // Set max brightness limit (caps all subsequent brightness operations)
    leds.SetMaxBrightness(100);  // Limit to ~39% max
    ESP_LOGI(TAG, "  MaxBrightness set to 100 (of 255)");

    leds.SetBrightnessPercent(100);  // Now 100% = raw 100, not 255
    os_delay_msec(400);

    // Reset max brightness to full range
    leds.SetMaxBrightness(255);
    leds.TurnOff();
}

// ---------------------------------------------------------------------------
// Example 4: Animations
// ---------------------------------------------------------------------------
static void example_animations() {
    ESP_LOGI(TAG, "=== Example 4: Animations ===");

    auto& leds = FLUX_API.leds;
    LedError err;

    // Solid color animation
    err = leds.StartAnimation(LedAnimation::SOLID, LedColors::CYAN);
    log_led_result("StartAnimation(SOLID, CYAN)", err);
    os_delay_msec(1000);

    // Blink animation -- requires periodic UpdateAnimation() calls
    err = leds.StartAnimation(LedAnimation::BLINK, LedColors::RED);
    log_led_result("StartAnimation(BLINK, RED)", err);
    for (int i = 0; i < 20; ++i) {
        leds.UpdateAnimation();
        os_delay_msec(50);
    }

    // Breath (fade in/out) animation
    err = leds.StartAnimation(LedAnimation::BREATH, LedColors::GREEN);
    log_led_result("StartAnimation(BREATH, GREEN)", err);
    for (int i = 0; i < 40; ++i) {
        leds.UpdateAnimation();
        os_delay_msec(50);
    }

    // Rainbow animation (cycles through color wheel)
    err = leds.StartAnimation(LedAnimation::RAINBOW);
    log_led_result("StartAnimation(RAINBOW)", err);
    for (int i = 0; i < 60; ++i) {
        leds.UpdateAnimation();
        os_delay_msec(50);
    }

    // Check animation state
    ESP_LOGI(TAG, "  Animation active: %s", leds.IsAnimationActive() ? "YES" : "NO");

    // Stop animation
    err = leds.StopAnimation();
    log_led_result("StopAnimation()", err);
    leds.TurnOff();
}

// ---------------------------------------------------------------------------
// Example 5: Status indicators
// ---------------------------------------------------------------------------
static void example_status_indicators() {
    ESP_LOGI(TAG, "=== Example 5: Status Indicators ===");

    auto& leds = FLUX_API.leds;

    // Boot sequence -- typically shown during initialization
    leds.IndicateBoot();
    ESP_LOGI(TAG, "  IndicateBoot()");
    for (int i = 0; i < 20; ++i) { leds.UpdateAnimation(); os_delay_msec(50); }

    // System ready -- green steady
    leds.IndicateReady();
    ESP_LOGI(TAG, "  IndicateReady()");
    for (int i = 0; i < 20; ++i) { leds.UpdateAnimation(); os_delay_msec(50); }

    // Warning state -- yellow/amber indication
    leds.IndicateWarning();
    ESP_LOGI(TAG, "  IndicateWarning()");
    for (int i = 0; i < 20; ++i) { leds.UpdateAnimation(); os_delay_msec(50); }

    // Error state -- red indication
    leds.IndicateError();
    ESP_LOGI(TAG, "  IndicateError()");
    for (int i = 0; i < 20; ++i) { leds.UpdateAnimation(); os_delay_msec(50); }

    // Calibration in progress
    leds.IndicateCalibration();
    ESP_LOGI(TAG, "  IndicateCalibration()");
    for (int i = 0; i < 20; ++i) { leds.UpdateAnimation(); os_delay_msec(50); }

    leds.StopAnimation();
    leds.TurnOff();
}

// ---------------------------------------------------------------------------
// Example 6: Color wheel utility
// ---------------------------------------------------------------------------
static void example_color_wheel() {
    ESP_LOGI(TAG, "=== Example 6: Color Wheel ===");

    auto& leds = FLUX_API.leds;

    // ColorWheel(position) generates a smooth rainbow, position 0-255
    for (uint8_t pos = 0; pos < 255; pos += 15) {
        uint32_t rgb = LedManager::ColorWheel(pos);
        leds.SetColor(rgb);
        ESP_LOGI(TAG, "  wheel[%3u] = 0x%06lX", pos, static_cast<unsigned long>(rgb));
        os_delay_msec(100);
    }

    leds.TurnOff();
}

// ---------------------------------------------------------------------------
// Example 7: LED info and diagnostics
// ---------------------------------------------------------------------------
static void example_diagnostics() {
    ESP_LOGI(TAG, "=== Example 7: LED Diagnostics ===");

    auto& leds = FLUX_API.leds;

    // Hardware info
    ESP_LOGI(TAG, "LED GPIO pin: %d", static_cast<int>(leds.GetCurrentGpioPin()));
    ESP_LOGI(TAG, "LED count:    %lu", static_cast<unsigned long>(leds.GetLedCount()));

    // Diagnostics snapshot
    LedSystemDiagnostics diag{};
    LedError err = leds.GetSystemDiagnostics(diag);
    if (err == LedError::SUCCESS) {
        ESP_LOGI(TAG, "System healthy:       %s", diag.system_healthy ? "YES" : "NO");
        ESP_LOGI(TAG, "LED initialized:      %s", diag.led_initialized ? "YES" : "NO");
        ESP_LOGI(TAG, "Animation active:     %s", diag.animation_active ? "YES" : "NO");
        ESP_LOGI(TAG, "Current animation:    %s", LedAnimationToString(diag.current_animation));
        ESP_LOGI(TAG, "Total operations:     %lu", static_cast<unsigned long>(diag.total_operations));
        ESP_LOGI(TAG, "Successful ops:       %lu", static_cast<unsigned long>(diag.successful_operations));
        ESP_LOGI(TAG, "Failed ops:           %lu", static_cast<unsigned long>(diag.failed_operations));
        ESP_LOGI(TAG, "Animation cycles:     %lu", static_cast<unsigned long>(diag.animation_cycles));
        ESP_LOGI(TAG, "Current brightness:   %lu", static_cast<unsigned long>(diag.current_brightness));
        ESP_LOGI(TAG, "Current color:        0x%06lX", static_cast<unsigned long>(diag.current_color));
    }

    // Full stats dump
    leds.DumpStatistics();
}

// ---------------------------------------------------------------------------
// app_main -- entry point
// ---------------------------------------------------------------------------
extern "C" void app_main(void) {
    ESP_LOGI(TAG, "Flux LedManager Example");
    ESP_LOGI(TAG, "=======================");

    auto& flux = Flux::GetInstance();
    if (!flux.EnsureInitialized()) {
        ESP_LOGE(TAG, "Flux initialization failed");
    }

    // Set conservative brightness for the examples
    FLUX_API.leds.SetBrightnessPercent(30);

    example_basic_colors();
    os_delay_msec(200);

    example_color_palette();
    os_delay_msec(200);

    example_brightness();
    os_delay_msec(200);

    example_animations();
    os_delay_msec(200);

    example_status_indicators();
    os_delay_msec(200);

    example_color_wheel();
    os_delay_msec(200);

    example_diagnostics();

    // Leave the LED off when done
    FLUX_API.leds.TurnOff();

    ESP_LOGI(TAG, "=======================");
    ESP_LOGI(TAG, "LedManager example complete");
}
