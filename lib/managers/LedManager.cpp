/**
 * @file LedManager.cpp
 * @brief Implementation of the Flux V1 LED manager (single WS2812 LED).
 *
 * @author HardFOC Team
 * @date 2026
 * @version 1.0
 */

#include "LedManager.h"

#include "core/hf-core-utils/hf-utils-rtos-wrap/include/OsUtility.h"

#include <cmath>

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
    led_animator_.reset();
    led_strip_.reset();
    is_initialized_.store(false, std::memory_order_release);

    Logger::GetInstance().Info(TAG, "LED manager shutdown");
    return true;
}

LedError LedManager::GetSystemDiagnostics(LedSystemDiagnostics& d) const noexcept {
    std::lock_guard<RtosMutex> lock(mutex_);
    d.system_healthy      = is_initialized_.load() && led_strip_ && led_animator_;
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

    led_strip_->SetPixel(led_index, color.ToRgb());
    esp_err_t result = led_strip_->Show();
    if (result != ESP_OK) {
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

    led_strip_->SetPixel(0, 0);
    esp_err_t result = led_strip_->Show();
    if (result != ESP_OK) {
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
    led_strip_->SetBrightness(raw);
    current_brightness_ = raw;
    esp_err_t result = led_strip_->Show();
    return (result == ESP_OK) ? (UpdateStatistics(true), LedError::SUCCESS)
                              : (UpdateStatistics(false), LedError::HARDWARE_ERROR);
}

LedError LedManager::SetBrightnessRaw(uint8_t brightness_raw) noexcept {
    if (!EnsureInitialized()) return LedError::NOT_INITIALIZED;

    std::lock_guard<RtosMutex> lock(mutex_);
    led_strip_->SetBrightness(brightness_raw);
    current_brightness_ = brightness_raw;
    esp_err_t result = led_strip_->Show();
    return (result == ESP_OK) ? (UpdateStatistics(true), LedError::SUCCESS)
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
    if (!led_strip_) return LedError::NOT_INITIALIZED;

    led_strip_->SetPixel(0, 0);
    led_strip_->Show();
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

    gpio_num_t led_gpio = GetLedGpioPin();
    current_led_gpio_ = led_gpio;

    led_strip_ = std::make_unique<WS2812Strip>(
        led_gpio, DEFAULT_RMT_CHANNEL, kNumLeds, LedType::RGB,
        14, 52, 52, 52, 255);

    if (!led_strip_) {
        Logger::GetInstance().Error(TAG, "Failed to create WS2812 strip");
        return false;
    }

    esp_err_t result = led_strip_->Begin();
    if (result != ESP_OK) {
        Logger::GetInstance().Error(TAG, "Failed to init WS2812: %s", esp_err_to_name(result));
        return false;
    }

    led_animator_ = std::make_unique<WS2812Animator>(*led_strip_, kNumLeds);
    if (!led_animator_) {
        Logger::GetInstance().Error(TAG, "Failed to create WS2812 animator");
        return false;
    }

    // Start dark
    led_strip_->SetPixel(0, 0);
    led_strip_->Show();

    system_start_time_ = GetSystemUptimeMs();
    is_initialized_.store(true, std::memory_order_release);

    Logger::GetInstance().Info(TAG, "LED manager initialized on GPIO %d", led_gpio);
    return true;
}

LedError LedManager::UpdateAnimationStep() noexcept {
    if (!led_strip_ || !led_animator_) return LedError::NOT_INITIALIZED;

    switch (current_animation_) {
        case LedAnimation::BLINK:
        case LedAnimation::STATUS_WARN:
        case LedAnimation::STATUS_ERROR:
        case LedAnimation::STATUS_CALIBRATE: {
            bool on = (animation_step_ / (BLINK_SPEED / ANIMATION_UPDATE_INTERVAL)) % 2 == 0;
            led_strip_->SetPixel(0, on ? current_color_.ToRgb() : 0);
            if (led_strip_->Show() != ESP_OK) return LedError::ANIMATION_FAILED;
            break;
        }
        case LedAnimation::BREATH:
        case LedAnimation::STATUS_BOOT: {
            uint8_t pct = static_cast<uint8_t>(50 + 50 * std::sin(animation_step_ * BREATH_SPEED * 0.01f));
            led_strip_->SetBrightness(PercentToRaw(pct));
            led_strip_->SetPixel(0, current_color_.ToRgb());
            if (led_strip_->Show() != ESP_OK) return LedError::ANIMATION_FAILED;
            break;
        }
        case LedAnimation::RAINBOW: {
            led_strip_->SetPixel(0, ColorWheel(animation_step_ % 256));
            if (led_strip_->Show() != ESP_OK) return LedError::ANIMATION_FAILED;
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
