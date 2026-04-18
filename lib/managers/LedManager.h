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
 * @version 2.0
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

/** @brief Error codes returned by LedManager operations. */
enum class LedError : uint8_t {
    SUCCESS = 0,               ///< Operation completed successfully.
    NOT_INITIALIZED,           ///< Manager has not been initialized yet.
    INITIALIZATION_FAILED,     ///< WS2812 strip/RMT setup failed.
    INVALID_PARAMETER,         ///< Argument out of valid range.
    HARDWARE_ERROR,            ///< RMT peripheral or strip communication error.
    ANIMATION_FAILED,          ///< Animation start/update failed.
    INVALID_COLOR,             ///< Colour value rejected.
    INVALID_BRIGHTNESS         ///< Brightness value out of [0, max] range.
};

/**
 * @brief Convert a LedError to a human-readable C-string.
 * @param error  The error code to convert.
 * @return Null-terminated string describing @p error.
 */
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

/** @brief Available LED animation modes. */
enum class LedAnimation : uint8_t {
    OFF = 0,           ///< LED turned off (no animation).
    SOLID,             ///< Constant colour, no animation.
    BLINK,             ///< On/off blink at BLINK_SPEED interval.
    BREATH,            ///< Smooth brightness ramp up/down.
    RAINBOW,           ///< Continuous colour-wheel cycle.
    STATUS_OK,         ///< System-ready indicator (solid green).
    STATUS_WARN,       ///< Warning indicator (yellow blink).
    STATUS_ERROR,      ///< Error indicator (red fast-blink).
    STATUS_BOOT,       ///< Boot-up indicator (blue blink).
    STATUS_CALIBRATE   ///< Calibration indicator (cyan breathe).
};

/**
 * @brief Convert a LedAnimation to a human-readable C-string.
 * @param animation  The animation type to convert.
 * @return Null-terminated string describing @p animation.
 */
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

/**
 * @brief Simple RGB colour triplet.
 * @details Stored as three 8-bit components.  Use ToRgb() / FromRgb() to
 *          convert to/from a packed 0xRRGGBB integer.
 */
struct LedColor {
    uint8_t red;   ///< Red channel   [0–255].
    uint8_t green; ///< Green channel [0–255].
    uint8_t blue;  ///< Blue channel  [0–255].

    /** @brief Default-construct black (0, 0, 0). */
    constexpr LedColor() noexcept : red(0), green(0), blue(0) {}

    /**
     * @brief Construct from individual R/G/B components.
     * @param r Red   [0–255].
     * @param g Green [0–255].
     * @param b Blue  [0–255].
     */
    constexpr LedColor(uint8_t r, uint8_t g, uint8_t b) noexcept : red(r), green(g), blue(b) {}

    /**
     * @brief Pack into a 24-bit 0xRRGGBB integer.
     * @return Packed 24-bit RGB value.
     */
    [[nodiscard]] constexpr uint32_t ToRgb() const noexcept {
        return (static_cast<uint32_t>(red) << 16) |
               (static_cast<uint32_t>(green) << 8) |
               static_cast<uint32_t>(blue);
    }

    /**
     * @brief Unpack from a 24-bit 0xRRGGBB integer.
     * @param rgb  Packed 24-bit colour.
     * @return LedColor with extracted R/G/B components.
     */
    static constexpr LedColor FromRgb(uint32_t rgb) noexcept {
        return LedColor(
            static_cast<uint8_t>((rgb >> 16) & 0xFF),
            static_cast<uint8_t>((rgb >> 8) & 0xFF),
            static_cast<uint8_t>(rgb & 0xFF));
    }
};

/** @brief Predefined named colour constants. */
namespace LedColors {
    static constexpr LedColor BLACK(0, 0, 0);         ///< Off / black.
    static constexpr LedColor RED(255, 0, 0);         ///< Pure red.
    static constexpr LedColor GREEN(0, 255, 0);       ///< Pure green.
    static constexpr LedColor BLUE(0, 0, 255);        ///< Pure blue.
    static constexpr LedColor YELLOW(255, 255, 0);    ///< Yellow.
    static constexpr LedColor CYAN(0, 255, 255);      ///< Cyan.
    static constexpr LedColor MAGENTA(255, 0, 255);   ///< Magenta.
    static constexpr LedColor WHITE(255, 255, 255);   ///< Full white.
    static constexpr LedColor ORANGE(255, 165, 0);    ///< Orange.
    static constexpr LedColor PURPLE(128, 0, 128);    ///< Purple.
} // namespace LedColors

//==============================================================================
// LED DIAGNOSTICS
//==============================================================================

/** @brief Snapshot of LED subsystem health and counters. */
struct LedSystemDiagnostics {
    bool system_healthy;               ///< true when driver initialised and operational.
    bool led_initialized;              ///< true after WS2812 strip init succeeds.
    bool animation_active;             ///< true if an animation is currently running.
    LedAnimation current_animation;    ///< Currently active animation mode.
    uint32_t total_operations;         ///< Lifetime count of all API calls.
    uint32_t successful_operations;    ///< Calls that returned SUCCESS.
    uint32_t failed_operations;        ///< Calls that returned an error.
    uint32_t animation_cycles;         ///< Number of animation update ticks processed.
    uint32_t current_brightness;       ///< Current brightness value [0–255].
    uint32_t current_color;            ///< Current colour as packed 0xRRGGBB.
};

//==============================================================================
// LED MANAGER
//==============================================================================

/**
 * @class LedManager
 * @brief Singleton that drives the on-board WS2812 LED via ESP-IDF RMT.
 *
 * @details Controls a single WS2812 NeoPixel LED on GPIO3.  Provides:
 *   - Solid-colour, blink, breathe, and rainbow animations
 *   - Brightness control (percent or raw 0–255)
 *   - Convenience status indicators (boot, ready, warning, error, calibrating)
 *   - Thread-safe via RtosMutex; statistics tracked atomically
 *
 * @note Call UpdateAnimation() periodically (e.g. every 50 ms from a task)
 *       to drive multi-frame animations.  Solid colours require no update.
 */
class LedManager {
public:
    //==========================================================================
    // SINGLETON AND LIFECYCLE
    //==========================================================================

    /**
     * @brief Access the singleton instance (Meyers singleton, thread-safe).
     * @return Reference to the sole LedManager instance.
     */
    static LedManager& GetInstance() noexcept;

    LedManager(const LedManager&) = delete;
    LedManager& operator=(const LedManager&) = delete;
    LedManager(LedManager&&) = delete;
    LedManager& operator=(LedManager&&) = delete;

    /**
     * @brief Initialise the WS2812 LED strip (idempotent).
     * @return true on success or if already initialised.
     */
    [[nodiscard]] bool EnsureInitialized() noexcept;

    /**
     * @brief Turn off the LED and release driver resources.
     * @return true on success.
     */
    [[nodiscard]] bool Shutdown() noexcept;

    /** @brief Alias for Shutdown(). */
    [[nodiscard]] bool Deinitialize() noexcept { return Shutdown(); }

    /** @brief Check whether the LED driver is initialised.
     *  @return true if ready. */
    [[nodiscard]] bool IsInitialized() const noexcept { return is_initialized_.load(std::memory_order_acquire); }

    /**
     * @brief Populate a diagnostics snapshot.
     * @param[out] diagnostics  Struct filled with current counters and state.
     * @return LedError::SUCCESS on success.
     */
    [[nodiscard]] LedError GetSystemDiagnostics(LedSystemDiagnostics& diagnostics) const noexcept;

    /**
     * @brief Get the most recent error code from any LED operation.
     * @return Last LedError set by any API call
     */
    [[nodiscard]] LedError GetLastError() const noexcept { return last_error_.load(std::memory_order_acquire); }

    //==========================================================================
    // BASIC LED OPERATIONS
    //==========================================================================

    /**
     * @brief Set LED colour from an LedColor struct.
     * @param color      RGB colour to display.
     * @param led_index  LED index (always 0 for single-LED boards).
     * @return LedError::SUCCESS on success.
     */
    [[nodiscard]] LedError SetColor(const LedColor& color, uint32_t led_index = 0) noexcept;

    /**
     * @brief Set LED colour from a packed 0xRRGGBB integer.
     * @param rgb        24-bit packed colour.
     * @param led_index  LED index (always 0 for single-LED boards).
     * @return LedError::SUCCESS on success.
     */
    [[nodiscard]] LedError SetColor(uint32_t rgb, uint32_t led_index = 0) noexcept;

    /**
     * @brief Turn the LED off (set colour to black).
     * @return LedError::SUCCESS on success.
     */
    [[nodiscard]] LedError TurnOff() noexcept;

    /**
     * @brief Read the currently displayed colour.
     * @param[out] color  Filled with current RGB value.
     * @return LedError::SUCCESS on success.
     */
    [[nodiscard]] LedError GetCurrentColor(LedColor& color) const noexcept;

    //==========================================================================
    // BRIGHTNESS
    //==========================================================================

    /**
     * @brief Set brightness as a percentage of max.
     * @param brightness_percent  Value in [0, 100].
     * @return LedError::SUCCESS on success, INVALID_BRIGHTNESS if out of range.
     */
    [[nodiscard]] LedError SetBrightnessPercent(uint8_t brightness_percent) noexcept;

    /**
     * @brief Set brightness as a raw 0–255 value.
     * @param brightness_raw  Raw brightness [0, max_brightness].
     * @return LedError::SUCCESS on success.
     */
    [[nodiscard]] LedError SetBrightnessRaw(uint8_t brightness_raw) noexcept;

    /** @brief Alias for SetBrightnessRaw(). */
    [[nodiscard]] LedError SetBrightness(uint8_t brightness) noexcept { return SetBrightnessRaw(brightness); }

    /**
     * @brief Set the upper ceiling for brightness.
     * @param max_brightness  Maximum raw brightness [0–255].
     * @return LedError::SUCCESS on success.
     */
    [[nodiscard]] LedError SetMaxBrightness(uint8_t max_brightness) noexcept;

    /**
     * @brief Read current brightness as a percentage of max.
     * @param[out] brightness_percent  Filled with value in [0, 100].
     * @return LedError::SUCCESS.
     */
    [[nodiscard]] LedError GetCurrentBrightnessPercent(uint8_t& brightness_percent) const noexcept {
        brightness_percent = RawToPercent(current_brightness_);
        return LedError::SUCCESS;
    }

    /**
     * @brief Read current brightness as a raw 0–255 value.
     * @param[out] brightness_raw  Filled with raw brightness.
     * @return LedError::SUCCESS.
     */
    [[nodiscard]] LedError GetCurrentBrightnessRaw(uint8_t& brightness_raw) const noexcept {
        brightness_raw = current_brightness_;
        return LedError::SUCCESS;
    }

    //==========================================================================
    // ANIMATION
    //==========================================================================

    /**
     * @brief Start a named animation with an optional base colour.
     * @param animation  Animation type to run.
     * @param color      Base colour for the animation (default: white).
     * @return LedError::SUCCESS on success.
     */
    [[nodiscard]] LedError StartAnimation(LedAnimation animation, const LedColor& color = LedColors::WHITE) noexcept;

    /**
     * @brief Stop the current animation and turn the LED off.
     * @return LedError::SUCCESS on success.
     */
    [[nodiscard]] LedError StopAnimation() noexcept;

    /** @brief Check whether an animation is currently running. */
    [[nodiscard]] bool IsAnimationActive() const noexcept { return current_animation_ != LedAnimation::OFF; }

    /**
     * @brief Advance the animation by one tick.  Call periodically (~50 ms).
     * @return LedError::SUCCESS on success, or ANIMATION_FAILED on error.
     */
    [[nodiscard]] LedError UpdateAnimation() noexcept;

    //==========================================================================
    // STATUS INDICATION
    //==========================================================================

    /**
     * @brief Set a status animation by enum (generic variant).
     * @param animation  The status animation to start.
     * @return LedError::SUCCESS on success.
     */
    [[nodiscard]] LedError SetStatus(LedAnimation animation) noexcept { return StartAnimation(animation); }

    /** @brief Show the boot indicator (blue blink). */
    [[nodiscard]] LedError IndicateBoot() noexcept { return StartAnimation(LedAnimation::STATUS_BOOT); }
    /** @brief Show the ready indicator (solid green). */
    [[nodiscard]] LedError IndicateReady() noexcept { return StartAnimation(LedAnimation::STATUS_OK); }
    /** @brief Show the warning indicator (yellow blink). */
    [[nodiscard]] LedError IndicateWarning() noexcept { return StartAnimation(LedAnimation::STATUS_WARN); }
    /** @brief Show the error indicator (red fast-blink). */
    [[nodiscard]] LedError IndicateError() noexcept { return StartAnimation(LedAnimation::STATUS_ERROR); }
    /** @brief Show the calibration indicator (cyan breathe). */
    [[nodiscard]] LedError IndicateCalibration() noexcept { return StartAnimation(LedAnimation::STATUS_CALIBRATE); }

    //==========================================================================
    // UTILITY
    //==========================================================================

    /**
     * @brief Map a 0–255 wheel position to a rainbow RGB colour.
     * @param position  Wheel position [0–255].
     * @return Packed 0xRRGGBB colour.
     */
    [[nodiscard]] static uint32_t ColorWheel(uint8_t position) noexcept;

    /** @brief Log current LED state and statistics to the console. */
    void DumpStatistics() const noexcept;

    /** @brief GPIO pin the LED strip is attached to. */
    [[nodiscard]] gpio_num_t GetCurrentGpioPin() const noexcept { return current_led_gpio_; }

    /** @brief Number of LEDs in the strip (always 1 on Flux V1). */
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

    [[nodiscard]] uint8_t RawToPercent(uint8_t raw) const noexcept {
        if (max_brightness_ == 0) return 0;
        return static_cast<uint8_t>((static_cast<uint32_t>(raw) * 100) / max_brightness_);
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

/**
 * @brief Convenience accessor — equivalent to LedManager::GetInstance().
 * @return Reference to the singleton LedManager.
 */
[[nodiscard]] inline LedManager& GetLedManager() noexcept {
    return LedManager::GetInstance();
}

#endif // FLUX_LED_MANAGER_H_
