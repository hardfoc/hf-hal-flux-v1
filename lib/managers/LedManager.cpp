/**
 * @file LedManager.cpp
 * @brief Implementation of the Flux V1 LED manager (single WS2812 LED).
 *
 * @author HardFOC Team
 * @date 2026
 * @version 1.0
 */

#include "LedManager.h"

#include "bsp_bind/FluxPlatformBind.h"
#include "bsp_bind/board/flux_v1/FluxBoardConfig.h"

#include "core/hf-core-utils/hf-utils-rtos-wrap/include/OsUtility.h"

#include <cmath>
#include <mutex>

static constexpr const char* TAG = "FluxLed";

//==============================================================================
// SINGLETON
//==============================================================================

LedManager& LedManager::GetInstance() noexcept {
    static LedManager instance;
    return instance;
}

//==============================================================================
// LIFECYCLE
//==============================================================================

bool LedManager::EnsureInitialized() noexcept {
    if (is_initialized_.load(std::memory_order_acquire)) return true;
    std::lock_guard<RtosMutex> lock(mutex_);
    if (is_initialized_.load(std::memory_order_acquire)) return true;
    return Initialize();
}

bool LedManager::Shutdown() noexcept {
    std::lock_guard<RtosMutex> lock(mutex_);
    if (!is_initialized_.load(std::memory_order_acquire)) return true;

    const LedError anim_err = StopAnimation();
    if (anim_err != LedError::SUCCESS) {
        Logger::GetInstance().Warn(TAG, "StopAnimation during shutdown: %s",
                                   LedErrorToString(anim_err));
    }
    const LedError off_err = TurnOff();
    if (off_err != LedError::SUCCESS) {
        Logger::GetInstance().Warn(TAG, "TurnOff during shutdown: %s",
                                   LedErrorToString(off_err));
    }
    flux::bsp::LedStripEnd();
    strip_ready_ = false;
    is_initialized_.store(false, std::memory_order_release);

    Logger::GetInstance().Info(TAG, "LED manager shutdown");
    return true;
}

LedError LedManager::GetSystemDiagnostics(LedSystemDiagnostics& d) const noexcept {
    std::lock_guard<RtosMutex> lock(mutex_);
    d.system_healthy      = is_initialized_.load() && strip_ready_;
    d.led_initialized     = is_initialized_.load();
    d.animation_active    = IsAnimationActive();
    d.current_animation   = current_animation_;
    d.total_operations    = total_operations_.load();
    d.successful_operations = successful_operations_.load();
    d.failed_operations   = failed_operations_.load();
    d.animation_cycles    = animation_cycles_.load();
    d.current_brightness  = current_brightness_;
    d.current_color       = current_color_.ToRgb();
    return LedError::SUCCESS;
}

//==============================================================================
// BASIC OPERATIONS
//==============================================================================

LedError LedManager::SetColor(const LedColor& color, uint32_t led_index) noexcept {
    if (!EnsureInitialized()) { UpdateLastError(LedError::NOT_INITIALIZED); return LedError::NOT_INITIALIZED; }
    if (led_index >= kNumLeds) { UpdateLastError(LedError::INVALID_PARAMETER); return LedError::INVALID_PARAMETER; }

    std::lock_guard<RtosMutex> lock(mutex_);

    if (current_animation_ != LedAnimation::OFF) {
        const LedError se = StopAnimation();
        if (se != LedError::SUCCESS) {
            UpdateLastError(se);
            return se;
        }
    }

    flux::bsp::LedStripSetPixel(led_index, color.ToRgb());
    const bool shown = flux::bsp::LedStripShow();
    if (!shown) {
        UpdateLastError(LedError::HARDWARE_ERROR);
        UpdateStatistics(false);
        return LedError::HARDWARE_ERROR;
    }

    if (led_index == 0) {
        current_color_ = color;
        current_animation_ = LedAnimation::SOLID;
    }
    UpdateStatistics(true);
    return LedError::SUCCESS;
}

LedError LedManager::SetColor(uint32_t rgb, uint32_t led_index) noexcept {
    return SetColor(LedColor::FromRgb(rgb), led_index);
}

LedError LedManager::TurnOff() noexcept {
    if (!EnsureInitialized()) { UpdateLastError(LedError::NOT_INITIALIZED); return LedError::NOT_INITIALIZED; }

    std::lock_guard<RtosMutex> lock(mutex_);
    if (current_animation_ != LedAnimation::OFF) {
        const LedError se = StopAnimation();
        if (se != LedError::SUCCESS) {
            UpdateLastError(se);
            return se;
        }
    }

    flux::bsp::LedStripSetPixel(0, 0);
    const bool shown = flux::bsp::LedStripShow();
    if (!shown) {
        UpdateLastError(LedError::HARDWARE_ERROR);
        UpdateStatistics(false);
        return LedError::HARDWARE_ERROR;
    }

    current_color_ = LedColors::BLACK;
    current_animation_ = LedAnimation::OFF;
    UpdateStatistics(true);
    return LedError::SUCCESS;
}

LedError LedManager::GetCurrentColor(LedColor& color) const noexcept {
    std::lock_guard<RtosMutex> lock(mutex_);
    color = current_color_;
    return LedError::SUCCESS;
}

//==============================================================================
// BRIGHTNESS
//==============================================================================

LedError LedManager::SetBrightnessPercent(uint8_t brightness_percent) noexcept {
    if (!EnsureInitialized()) return LedError::NOT_INITIALIZED;
    if (brightness_percent > 100) return LedError::INVALID_BRIGHTNESS;

    std::lock_guard<RtosMutex> lock(mutex_);
    uint8_t raw = PercentToRaw(brightness_percent);
    flux::bsp::LedStripSetBrightness(raw);
    current_brightness_ = raw;
    const bool shown = flux::bsp::LedStripShow();
    return shown ? (UpdateStatistics(true), LedError::SUCCESS)
                              : (UpdateStatistics(false), LedError::HARDWARE_ERROR);
}

LedError LedManager::SetBrightnessRaw(uint8_t brightness_raw) noexcept {
    if (!EnsureInitialized()) return LedError::NOT_INITIALIZED;

    std::lock_guard<RtosMutex> lock(mutex_);
    flux::bsp::LedStripSetBrightness(brightness_raw);
    current_brightness_ = brightness_raw;
    const bool shown = flux::bsp::LedStripShow();
    return shown ? (UpdateStatistics(true), LedError::SUCCESS)
                              : (UpdateStatistics(false), LedError::HARDWARE_ERROR);
}

LedError LedManager::SetMaxBrightness(uint8_t max_brightness) noexcept {
    if (!EnsureInitialized()) return LedError::NOT_INITIALIZED;
    std::lock_guard<RtosMutex> lock(mutex_);
    max_brightness_ = max_brightness;
    return LedError::SUCCESS;
}

//==============================================================================
// ANIMATION
//==============================================================================

LedError LedManager::StartAnimation(LedAnimation animation, const LedColor& color) noexcept {
    if (!EnsureInitialized()) return LedError::NOT_INITIALIZED;

    std::lock_guard<RtosMutex> lock(mutex_);
    if (current_animation_ != LedAnimation::OFF) {
        const LedError se = StopAnimation();
        if (se != LedError::SUCCESS) {
            UpdateLastError(se);
            return se;
        }
    }

    current_animation_ = animation;
    current_color_ = color;
    animation_step_ = 0;
    animation_direction_ = 1;
    last_animation_update_ = GetSystemUptimeMs();

    switch (animation) {
        case LedAnimation::STATUS_OK:        current_color_ = LedColors::GREEN;  break;
        case LedAnimation::STATUS_WARN:      current_color_ = LedColors::YELLOW; break;
        case LedAnimation::STATUS_ERROR:     current_color_ = LedColors::RED;    break;
        case LedAnimation::STATUS_BOOT:      current_color_ = LedColors::BLUE;   break;
        case LedAnimation::STATUS_CALIBRATE: current_color_ = LedColors::PURPLE; break;
        default: break;
    }

    UpdateStatistics(true);
    return LedError::SUCCESS;
}

LedError LedManager::StopAnimation() noexcept {
    std::lock_guard<RtosMutex> lock(mutex_);
    if (current_animation_ == LedAnimation::OFF) return LedError::SUCCESS;
    if (!strip_ready_) return LedError::NOT_INITIALIZED;

    flux::bsp::LedStripSetPixel(0, 0);
    (void)flux::bsp::LedStripShow();
    current_animation_ = LedAnimation::OFF;
    current_color_ = LedColors::BLACK;
    animation_step_ = 0;
    return LedError::SUCCESS;
}

LedError LedManager::UpdateAnimation() noexcept {
    if (!EnsureInitialized()) return LedError::NOT_INITIALIZED;
    std::lock_guard<RtosMutex> lock(mutex_);
    if (current_animation_ == LedAnimation::OFF) return LedError::SUCCESS;

    uint64_t now = GetSystemUptimeMs();
    if (now - last_animation_update_ < ANIMATION_UPDATE_INTERVAL) return LedError::SUCCESS;
    last_animation_update_ = now;

    LedError err = UpdateAnimationStep();
    if (err == LedError::SUCCESS) animation_cycles_.fetch_add(1, std::memory_order_relaxed);
    return err;
}

//==============================================================================
// UTILITY
//==============================================================================

uint32_t LedManager::ColorWheel(uint8_t position) noexcept {
    position = 255 - position;
    if (position < 85) {
        return (255 - position * 3) << 16 | (position * 3) << 8;
    } else if (position < 170) {
        position -= 85;
        return (position * 3) << 16 | (255 - position * 3) << 8;
    } else {
        position -= 170;
        return (255 - position * 3) << 8 | (position * 3);
    }
}

void LedManager::DumpStatistics() const noexcept {
    auto& log = Logger::GetInstance();
    log.Info(TAG, "=== Flux LED Manager Statistics ===");
    log.Info(TAG, "  Init: %s  GPIO: %d  LEDs: %u",
             is_initialized_.load() ? "YES" : "NO", current_led_gpio_, kNumLeds);
    log.Info(TAG, "  Animation: %s  Color: 0x%06X  Brightness: %u",
             LedAnimationToString(current_animation_), current_color_.ToRgb(), current_brightness_);
    log.Info(TAG, "  Ops total/ok/fail: %u/%u/%u  Cycles: %u",
             total_operations_.load(), successful_operations_.load(),
             failed_operations_.load(), animation_cycles_.load());
    log.Info(TAG, "=== End Flux LED Manager Statistics ===");
}

//==============================================================================
// PRIVATE
//==============================================================================

bool LedManager::Initialize() noexcept {
    Logger::GetInstance().Info(TAG, "Initializing Flux LED manager");

    const hf_pin_num_t led_gpio = GetLedGpioPin();
    current_led_gpio_ = led_gpio;

    const flux::bsp::LedStripSpec spec{led_gpio, flux::board::kStatusLedChannel,
                                       static_cast<uint16_t>(kNumLeds)};
    if (!flux::bsp::LedStripBegin(spec)) {
        Logger::GetInstance().Error(TAG, "Failed to init the WS2812 strip on GPIO %d", static_cast<int>(led_gpio));
        return false;
    }
    strip_ready_ = true;

    // Start dark
    flux::bsp::LedStripSetPixel(0, 0);
    (void)flux::bsp::LedStripShow();

    system_start_time_ = GetSystemUptimeMs();
    is_initialized_.store(true, std::memory_order_release);

    Logger::GetInstance().Info(TAG, "LED manager initialized on GPIO %d", static_cast<int>(led_gpio));
    return true;
}

LedError LedManager::UpdateAnimationStep() noexcept {
    if (!strip_ready_) return LedError::NOT_INITIALIZED;

    switch (current_animation_) {
        case LedAnimation::BLINK:
        case LedAnimation::STATUS_WARN:
        case LedAnimation::STATUS_ERROR:
        case LedAnimation::STATUS_CALIBRATE: {
            bool on = (animation_step_ / (BLINK_SPEED / ANIMATION_UPDATE_INTERVAL)) % 2 == 0;
            flux::bsp::LedStripSetPixel(0, on ? current_color_.ToRgb() : 0);
            if (!flux::bsp::LedStripShow()) return LedError::ANIMATION_FAILED;
            break;
        }
        case LedAnimation::BREATH:
        case LedAnimation::STATUS_BOOT: {
            uint8_t pct = static_cast<uint8_t>(50 + 50 * std::sin(animation_step_ * BREATH_SPEED * 0.01f));
            flux::bsp::LedStripSetBrightness(PercentToRaw(pct));
            flux::bsp::LedStripSetPixel(0, current_color_.ToRgb());
            if (!flux::bsp::LedStripShow()) return LedError::ANIMATION_FAILED;
            break;
        }
        case LedAnimation::RAINBOW: {
            flux::bsp::LedStripSetPixel(0, ColorWheel(animation_step_ % 256));
            if (!flux::bsp::LedStripShow()) return LedError::ANIMATION_FAILED;
            break;
        }
        case LedAnimation::SOLID:
        case LedAnimation::STATUS_OK:
        default:
            break;
    }
    animation_step_++;
    return LedError::SUCCESS;
}

void LedManager::UpdateStatistics(bool success) noexcept {
    total_operations_.fetch_add(1, std::memory_order_relaxed);
    if (success) successful_operations_.fetch_add(1, std::memory_order_relaxed);
    else         failed_operations_.fetch_add(1, std::memory_order_relaxed);
}

void LedManager::UpdateLastError(LedError error_code) noexcept {
    last_error_.store(error_code, std::memory_order_release);
}

uint64_t LedManager::GetSystemUptimeMs() const noexcept {
    return static_cast<uint64_t>(os_get_elapsed_time_msec());
}
