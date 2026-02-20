/**
 * @file LedManager.h
 * @brief LED management for the HardFOC Flux V1 platform (single WS2812).
 *
 * @details Identical hardware to Vortex: a single WS2812 LED on GPIO3 using
 *          the RMT peripheral. Provides solid/blink/breath/rainbow animations
 *          plus convenience status-indication methods (boot, ready, error…).
 *
 *          Follows the same stack-friendly singleton pattern as the other
 *          Flux managers. The WS2812Strip and WS2812Animator are heap-
 *          allocated because they are driver objects whose sizes depend on
 *          RMT configuration, but everything else is value-type.
 *
 * @author HardFOC Team
 * @date 2026
 * @version 1.0
 */

#ifndef FLUX_LED_MANAGER_H_
#define FLUX_LED_MANAGER_H_

#include "core/hf-core-drivers/external/hf-ws2812-rmt-driver/inc/ws2812_cpp.hpp"
#include "core/hf-core-drivers/external/hf-ws2812-rmt-driver/inc/ws2812_effects.hpp"
#include "handlers/logger/Logger.h"
#include "RtosMutex.h"
#include "core/hf-core-drivers/internal/hf-pincfg/src/hf_functional_pin_config_flux_v1.hpp"

#include <cstdint>
#include <atomic>
#include <memory>

//==============================================================================
// LED ERROR CODES
//==============================================================================

enum class LedError : uint8_t {
    SUCCESS = 0,
    NOT_INITIALIZED,
    INITIALIZATION_FAILED,
    INVALID_PARAMETER,
    HARDWARE_ERROR,
    ANIMATION_FAILED,
    INVALID_COLOR,
    INVALID_BRIGHTNESS
};

constexpr const char* LedErrorToString(LedError error) noexcept {
    switch (error) {
        case LedError::SUCCESS:               return "Success";
        case LedError::NOT_INITIALIZED:       return "Not initialized";
        case LedError::INITIALIZATION_FAILED: return "Initialization failed";
        case LedError::INVALID_PARAMETER:     return "Invalid parameter";
        case LedError::HARDWARE_ERROR:        return "Hardware error";
        case LedError::ANIMATION_FAILED:      return "Animation failed";
        case LedError::INVALID_COLOR:         return "Invalid color";
        case LedError::INVALID_BRIGHTNESS:    return "Invalid brightness";
        default:                              return "Unknown error";
    }
}

//==============================================================================
// LED ANIMATION TYPES
//==============================================================================

enum class LedAnimation : uint8_t {
    OFF = 0,
    SOLID,
    BLINK,
    BREATH,
    RAINBOW,
    STATUS_OK,
    STATUS_WARN,
    STATUS_ERROR,
    STATUS_BOOT,
    STATUS_CALIBRATE
};

constexpr const char* LedAnimationToString(LedAnimation animation) noexcept {
    switch (animation) {
        case LedAnimation::OFF:              return "Off";
        case LedAnimation::SOLID:            return "Solid";
        case LedAnimation::BLINK:            return "Blink";
        case LedAnimation::BREATH:           return "Breath";
        case LedAnimation::RAINBOW:          return "Rainbow";
        case LedAnimation::STATUS_OK:        return "Status OK";
        case LedAnimation::STATUS_WARN:      return "Status Warning";
        case LedAnimation::STATUS_ERROR:     return "Status Error";
        case LedAnimation::STATUS_BOOT:      return "Status Boot";
        case LedAnimation::STATUS_CALIBRATE: return "Status Calibrate";
        default:                             return "Unknown";
    }
}

//==============================================================================
// COLOR HELPERS
//==============================================================================

struct LedColor {
    uint8_t red;
    uint8_t green;
    uint8_t blue;

    constexpr LedColor() noexcept : red(0), green(0), blue(0) {}
    constexpr LedColor(uint8_t r, uint8_t g, uint8_t b) noexcept : red(r), green(g), blue(b) {}

    [[nodiscard]] constexpr uint32_t ToRgb() const noexcept {
        return (static_cast<uint32_t>(red) << 16) |
               (static_cast<uint32_t>(green) << 8) |
               static_cast<uint32_t>(blue);
    }

    static constexpr LedColor FromRgb(uint32_t rgb) noexcept {
        return LedColor(
            static_cast<uint8_t>((rgb >> 16) & 0xFF),
            static_cast<uint8_t>((rgb >> 8) & 0xFF),
            static_cast<uint8_t>(rgb & 0xFF));
    }
};

namespace LedColors {
    static constexpr LedColor BLACK(0, 0, 0);
    static constexpr LedColor RED(255, 0, 0);
    static constexpr LedColor GREEN(0, 255, 0);
    static constexpr LedColor BLUE(0, 0, 255);
    static constexpr LedColor YELLOW(255, 255, 0);
    static constexpr LedColor CYAN(0, 255, 255);
    static constexpr LedColor MAGENTA(255, 0, 255);
    static constexpr LedColor WHITE(255, 255, 255);
    static constexpr LedColor ORANGE(255, 165, 0);
    static constexpr LedColor PURPLE(128, 0, 128);
} // namespace LedColors

//==============================================================================
// LED DIAGNOSTICS
//==============================================================================

struct LedSystemDiagnostics {
    bool system_healthy;
    bool led_initialized;
    bool animation_active;
    LedAnimation current_animation;
    uint32_t total_operations;
    uint32_t successful_operations;
    uint32_t failed_operations;
    uint32_t animation_cycles;
    uint32_t current_brightness;
    uint32_t current_color;
};

//==============================================================================
// LED MANAGER
//==============================================================================

class LedManager {
public:
    //==========================================================================
    // SINGLETON AND LIFECYCLE
    //==========================================================================

    static LedManager& GetInstance() noexcept;

    LedManager(const LedManager&) = delete;
    LedManager& operator=(const LedManager&) = delete;
    LedManager(LedManager&&) = delete;
    LedManager& operator=(LedManager&&) = delete;

    [[nodiscard]] bool EnsureInitialized() noexcept;
    [[nodiscard]] bool Shutdown() noexcept;
    [[nodiscard]] bool IsInitialized() const noexcept { return is_initialized_.load(std::memory_order_acquire); }
    [[nodiscard]] LedError GetSystemDiagnostics(LedSystemDiagnostics& diagnostics) const noexcept;

    //==========================================================================
    // BASIC LED OPERATIONS
    //==========================================================================

    [[nodiscard]] LedError SetColor(const LedColor& color, uint32_t led_index = 0) noexcept;
    [[nodiscard]] LedError SetColor(uint32_t rgb, uint32_t led_index = 0) noexcept;
    [[nodiscard]] LedError TurnOff() noexcept;
    [[nodiscard]] LedError GetCurrentColor(LedColor& color) const noexcept;

    //==========================================================================
    // BRIGHTNESS
    //==========================================================================

    [[nodiscard]] LedError SetBrightnessPercent(uint8_t brightness_percent) noexcept;
    [[nodiscard]] LedError SetBrightnessRaw(uint8_t brightness_raw) noexcept;
    [[nodiscard]] LedError SetBrightness(uint8_t brightness) noexcept { return SetBrightnessRaw(brightness); }
    [[nodiscard]] LedError SetMaxBrightness(uint8_t max_brightness) noexcept;

    //==========================================================================
    // ANIMATION
    //==========================================================================

    [[nodiscard]] LedError StartAnimation(LedAnimation animation, const LedColor& color = LedColors::WHITE) noexcept;
    [[nodiscard]] LedError StopAnimation() noexcept;
    [[nodiscard]] bool IsAnimationActive() const noexcept { return current_animation_ != LedAnimation::OFF; }
    [[nodiscard]] LedError UpdateAnimation() noexcept;

    //==========================================================================
    // STATUS INDICATION
    //==========================================================================

    [[nodiscard]] LedError IndicateBoot() noexcept { return StartAnimation(LedAnimation::STATUS_BOOT); }
    [[nodiscard]] LedError IndicateReady() noexcept { return StartAnimation(LedAnimation::STATUS_OK); }
    [[nodiscard]] LedError IndicateWarning() noexcept { return StartAnimation(LedAnimation::STATUS_WARN); }
    [[nodiscard]] LedError IndicateError() noexcept { return StartAnimation(LedAnimation::STATUS_ERROR); }
    [[nodiscard]] LedError IndicateCalibration() noexcept { return StartAnimation(LedAnimation::STATUS_CALIBRATE); }

    //==========================================================================
    // UTILITY
    //==========================================================================

    [[nodiscard]] static uint32_t ColorWheel(uint8_t position) noexcept;
    void DumpStatistics() const noexcept;
    [[nodiscard]] gpio_num_t GetCurrentGpioPin() const noexcept { return current_led_gpio_; }
    [[nodiscard]] uint32_t GetLedCount() const noexcept { return kNumLeds; }

private:
    LedManager() noexcept = default;
    ~LedManager() = default;

    [[nodiscard]] bool Initialize() noexcept;
    [[nodiscard]] LedError UpdateAnimationStep() noexcept;
    void UpdateStatistics(bool success) noexcept;
    void UpdateLastError(LedError error_code) noexcept;
    [[nodiscard]] uint64_t GetSystemUptimeMs() const noexcept;

    [[nodiscard]] uint8_t PercentToRaw(uint8_t percent) const noexcept {
        if (percent > 100) percent = 100;
        return static_cast<uint8_t>((static_cast<uint32_t>(percent) * max_brightness_) / 100);
    }

    [[nodiscard]] static gpio_num_t GetLedGpioPin() noexcept {
        const auto* mapping = GetGpioMapping(HfFunctionalGpioPin::WS2812_LED_DAT);
        if (mapping && mapping->chip_type == static_cast<uint8_t>(HfGpioChipType::ESP32_INTERNAL)) {
            return static_cast<gpio_num_t>(mapping->physical_pin);
        }
        return GPIO_NUM_3;  // Fallback
    }

    //==========================================================================
    // CONSTANTS
    //==========================================================================

    static constexpr int    DEFAULT_RMT_CHANNEL         = 0;
    static constexpr uint32_t kNumLeds                  = 1;
    static constexpr uint8_t  DEFAULT_MAX_BRIGHTNESS    = 255;
    static constexpr uint32_t ANIMATION_UPDATE_INTERVAL = 50;   // ms
    static constexpr uint32_t BREATH_SPEED              = 5;
    static constexpr uint32_t BLINK_SPEED               = 500;  // ms

    //==========================================================================
    // STATE
    //==========================================================================

    std::atomic<bool> is_initialized_{false};
    mutable RtosMutex mutex_;

    std::unique_ptr<WS2812Strip>     led_strip_;
    std::unique_ptr<WS2812Animator>  led_animator_;

    LedAnimation current_animation_{LedAnimation::OFF};
    LedColor     current_color_{LedColors::BLACK};
    uint8_t      current_brightness_{255};
    uint8_t      max_brightness_{DEFAULT_MAX_BRIGHTNESS};
    uint16_t     animation_step_{0};
    int8_t       animation_direction_{1};
    uint64_t     last_animation_update_{0};

    std::atomic<uint32_t> total_operations_{0};
    std::atomic<uint32_t> successful_operations_{0};
    std::atomic<uint32_t> failed_operations_{0};
    std::atomic<uint32_t> animation_cycles_{0};
    std::atomic<LedError> last_error_{LedError::SUCCESS};
    uint64_t     system_start_time_{0};
    gpio_num_t   current_led_gpio_{GPIO_NUM_3};
};

//==============================================================================
// CONVENIENCE
//==============================================================================

[[nodiscard]] inline LedManager& GetLedManager() noexcept {
    return LedManager::GetInstance();
}

#endif // FLUX_LED_MANAGER_H_
