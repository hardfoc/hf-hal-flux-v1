/**
 * @file GpioManager.cpp
 * @brief Implementation of the Flux V1 GPIO manager.
 *
 * @author HardFOC Team
 * @date 2026
 * @version 1.0
 */

#include "GpioManager.h"
#include "handlers/logger/Logger.h"

static constexpr const char* TAG = "FluxGpio";

//==============================================================================
// SINGLETON
//==============================================================================

GpioManager& GpioManager::GetInstance() noexcept {
    static GpioManager instance;
    return instance;
}

//==============================================================================
// INITIALIZATION
//==============================================================================

bool GpioManager::EnsureInitialized() noexcept {
    if (initialized_.load(std::memory_order_acquire)) {
        return true;
    }
    MutexLockGuard lock(mutex_);
    if (initialized_.load(std::memory_order_acquire)) {
        return true;
    }
    bool ok = Initialize();
    initialized_.store(ok, std::memory_order_release);
    return ok;
}

bool GpioManager::Initialize() noexcept {
    Logger::GetInstance().Info(TAG, "Initializing Flux GPIO manager");

    registered_count_ = 0;

    // Iterate through all pins defined in the pin config
    for (size_t i = 0; i < HF_GPIO_MAPPING_SIZE; ++i) {
        const auto& mapping = HF_GPIO_MAPPING[i];
        auto category = static_cast<HfPinCategory>(mapping.category);

        // Only register GPIO and USER category pins
        if (!should_register_as_gpio(category)) {
            continue;
        }

        // Only ESP32 internal pins for the Flux board GPIO manager
        if (mapping.chip_type != static_cast<uint8_t>(HfGpioChipType::ESP32_INTERNAL)) {
            continue;
        }

        auto& entry = entries_[i];
        entry.functional_pin = static_cast<HfFunctionalGpioPin>(i);

        // Configure the EspGpio driver from the mapping data
        hf_gpio_config_t gpio_cfg = {};
        gpio_cfg.pin_number = mapping.physical_pin;
        gpio_cfg.direction  = hf_gpio_direction_t::HF_GPIO_DIRECTION_OUTPUT;

        if (mapping.has_pull) {
            gpio_cfg.pull_mode = mapping.pull_is_up
                ? hf_gpio_pull_mode_t::HF_GPIO_PULL_UP
                : hf_gpio_pull_mode_t::HF_GPIO_PULL_DOWN;
        } else {
            gpio_cfg.pull_mode = hf_gpio_pull_mode_t::HF_GPIO_FLOATING;
        }

        gpio_cfg.output_mode = mapping.is_push_pull
            ? hf_gpio_output_mode_t::HF_GPIO_PUSH_PULL
            : hf_gpio_output_mode_t::HF_GPIO_OPEN_DRAIN;

        entry.driver = EspGpio(gpio_cfg);

        if (mapping.is_inverted) {
            entry.driver.SetActiveState(hf_gpio_active_state_t::HF_GPIO_ACTIVE_LOW);
        } else {
            entry.driver.SetActiveState(hf_gpio_active_state_t::HF_GPIO_ACTIVE_HIGH);
        }

        if (entry.driver.EnsureInitialized()) {
            entry.registered = true;
            ++registered_count_;
            Logger::GetInstance().Info(TAG, "Registered GPIO: %.*s (GPIO%d)",
                                      static_cast<int>(mapping.name.size()), mapping.name.data(),
                                      mapping.physical_pin);
        } else {
            Logger::GetInstance().Error(TAG, "Failed to register GPIO: %.*s (GPIO%d)",
                                       static_cast<int>(mapping.name.size()), mapping.name.data(),
                                       mapping.physical_pin);
        }
    }

    Logger::GetInstance().Info(TAG, "GPIO manager initialized: %zu pins registered", registered_count_);
    return registered_count_ > 0;
}

//==============================================================================
// LOOKUP
//==============================================================================

size_t GpioManager::FindByName(std::string_view name) const noexcept {
    for (size_t i = 0; i < HF_GPIO_MAPPING_SIZE; ++i) {
        if (entries_[i].registered && HF_GPIO_MAPPING[i].name == name) {
            return i;
        }
    }
    return kInvalidIndex;
}

BaseGpio* GpioManager::Get(std::string_view name) noexcept {
    size_t idx = FindByName(name);
    if (idx == kInvalidIndex) return nullptr;
    return &entries_[idx].driver;
}

BaseGpio* GpioManager::Get(HfFunctionalGpioPin pin) noexcept {
    size_t idx = static_cast<size_t>(pin);
    if (idx >= kMaxEntries || !entries_[idx].registered) return nullptr;
    return &entries_[idx].driver;
}

bool GpioManager::Contains(std::string_view name) const noexcept {
    return FindByName(name) != kInvalidIndex;
}

size_t GpioManager::Size() const noexcept {
    return registered_count_;
}

//==============================================================================
// BASIC GPIO OPERATIONS
//==============================================================================

hf_gpio_err_t GpioManager::Set(std::string_view name, bool value) noexcept {
    auto* gpio = Get(name);
    if (!gpio) return hf_gpio_err_t::GPIO_ERROR_NOT_FOUND;
    return value ? gpio->SetActive() : gpio->SetInactive();
}

hf_gpio_err_t GpioManager::SetActive(std::string_view name) noexcept {
    auto* gpio = Get(name);
    if (!gpio) return hf_gpio_err_t::GPIO_ERROR_NOT_FOUND;
    return gpio->SetActive();
}

hf_gpio_err_t GpioManager::SetInactive(std::string_view name) noexcept {
    auto* gpio = Get(name);
    if (!gpio) return hf_gpio_err_t::GPIO_ERROR_NOT_FOUND;
    return gpio->SetInactive();
}

hf_gpio_err_t GpioManager::Read(std::string_view name, bool& state) noexcept {
    auto* gpio = Get(name);
    if (!gpio) return hf_gpio_err_t::GPIO_ERROR_NOT_FOUND;
    return gpio->Read(state);
}

hf_gpio_err_t GpioManager::Toggle(std::string_view name) noexcept {
    auto* gpio = Get(name);
    if (!gpio) return hf_gpio_err_t::GPIO_ERROR_NOT_FOUND;
    return gpio->Toggle();
}

hf_gpio_err_t GpioManager::IsActive(std::string_view name, bool& active) noexcept {
    auto* gpio = Get(name);
    if (!gpio) return hf_gpio_err_t::GPIO_ERROR_NOT_FOUND;
    return gpio->IsActive(active);
}

//==============================================================================
// PIN CONFIGURATION
//==============================================================================

hf_gpio_err_t GpioManager::SetDirection(std::string_view name, hf_gpio_direction_t direction) noexcept {
    auto* gpio = Get(name);
    if (!gpio) return hf_gpio_err_t::GPIO_ERROR_NOT_FOUND;
    return gpio->SetDirection(direction);
}

hf_gpio_err_t GpioManager::SetPullMode(std::string_view name, hf_gpio_pull_mode_t pull_mode) noexcept {
    auto* gpio = Get(name);
    if (!gpio) return hf_gpio_err_t::GPIO_ERROR_NOT_FOUND;
    return gpio->SetPullMode(pull_mode);
}

hf_gpio_err_t GpioManager::SetOutputMode(std::string_view name, hf_gpio_output_mode_t mode) noexcept {
    auto* gpio = Get(name);
    if (!gpio) return hf_gpio_err_t::GPIO_ERROR_NOT_FOUND;
    return gpio->SetOutputMode(mode);
}

//==============================================================================
// DIAGNOSTICS
//==============================================================================

void GpioManager::DumpStatistics() const noexcept {
    auto& log = Logger::GetInstance();
    log.Info(TAG, "=== Flux GPIO Manager Statistics ===");
    log.Info(TAG, "  Registered pins: %zu", registered_count_);

    for (size_t i = 0; i < HF_GPIO_MAPPING_SIZE; ++i) {
        if (entries_[i].registered) {
            const auto& mapping = HF_GPIO_MAPPING[i];
            log.Info(TAG, "  [%zu] %.*s -> GPIO%d (accesses=%u, errors=%u)",
                     i,
                     static_cast<int>(mapping.name.size()), mapping.name.data(),
                     mapping.physical_pin,
                     entries_[i].access_count,
                     entries_[i].error_count);
        }
    }
    log.Info(TAG, "=== End Flux GPIO Statistics ===");
}
