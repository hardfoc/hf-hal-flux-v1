/**
 * @file GpioManager.cpp
 * @brief Implementation of the Flux V1 GPIO manager.
 *
 * @author HardFOC Team
 * @date 2026
 * @version 1.0
 */

#include "GpioManager.h"
#include "bsp_bind/FluxPlatformBind.h"
#include "bsp_bind/board/flux_v1/FluxBoardConfig.h"
#include "handlers/logger/Logger.h"
#include "handlers/logger/LogDecorators.h"
#include "core/hf-core-utils/hf-utils-rtos-wrap/include/OsUtility.h"

static constexpr const char* TAG = "FluxGpio";

static_assert(static_cast<size_t>(HfFunctionalGpioPin::HF_FUNCTIONAL_GPIO_COUNT) <= flux::bsp::kMaxGpioSlots,
              "flux::bsp::kMaxGpioSlots must cover every functional GPIO");

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
    if (ok) {
        manager_error_.store(flux::hal::ManagerError::None, std::memory_order_release);
    }
    initialized_.store(ok, std::memory_order_release);
    return ok;
}

bool GpioManager::Initialize() noexcept {
    Logger::GetInstance().Info(TAG, "Initializing Flux GPIO manager");
    logger_decor::Section(TAG, "GPIO registration table");
    Logger::GetInstance().Info(TAG, "  %-28s %-7s %-7s", "Name", "Pin", "Result");
    Logger::GetInstance().Info(TAG, "  %-28s %-7s %-7s", "----", "---", "------");

    // A previous failed attempt may have left pins registered; start from nothing.
    ReleaseLocked();
    init_time_ms_ = os_get_elapsed_time_msec();
    size_t expected = 0;

    // Iterate through all pins defined in the pin config
    for (size_t i = 0; i < HF_GPIO_MAPPING_SIZE; ++i) {
        const auto& mapping = HF_GPIO_MAPPING[i];
        auto category = static_cast<HfPinCategory>(mapping.category);

        // Only register GPIO and USER category pins
        if (!should_register_as_gpio(category)) {
            continue;
        }

        // Only MCU pins for the Flux board GPIO manager
        if (mapping.chip_type != static_cast<uint8_t>(HfGpioChipType::ESP32_INTERNAL)) {
            continue;
        }

        ++expected;
        auto& entry = entries_[i];
        entry.functional_pin = static_cast<HfFunctionalGpioPin>(i);

        // Configure the GPIO from the mapping data. The pin map has no direction field: the
        // board config names the inputs (the valve drivers' FAULT_N lines), which are created
        // as inputs so the MCU never drives them, not even before the valve manager runs.
        const auto fpin = static_cast<HfFunctionalGpioPin>(i);
        hf_gpio_direction_t direction = flux::board::IsInputPin(fpin)
                                            ? hf_gpio_direction_t::HF_GPIO_DIRECTION_INPUT
                                            : hf_gpio_direction_t::HF_GPIO_DIRECTION_OUTPUT;

        hf_gpio_pull_mode_t pull_mode;
        if (mapping.has_pull) {
            pull_mode = mapping.pull_is_up
                ? hf_gpio_pull_mode_t::HF_GPIO_PULL_MODE_UP
                : hf_gpio_pull_mode_t::HF_GPIO_PULL_MODE_DOWN;
        } else {
            pull_mode = hf_gpio_pull_mode_t::HF_GPIO_PULL_MODE_FLOATING;
        }

        hf_gpio_output_mode_t output_mode = mapping.is_push_pull
            ? hf_gpio_output_mode_t::HF_GPIO_OUTPUT_MODE_PUSH_PULL
            : hf_gpio_output_mode_t::HF_GPIO_OUTPUT_MODE_OPEN_DRAIN;

        hf_gpio_active_state_t active_state = mapping.is_inverted
            ? hf_gpio_active_state_t::HF_GPIO_ACTIVE_LOW
            : hf_gpio_active_state_t::HF_GPIO_ACTIVE_HIGH;

        const flux::bsp::GpioSpec spec{static_cast<hf_pin_num_t>(mapping.physical_pin),
                                       direction, active_state, output_mode, pull_mode};
        entry.driver = flux::bsp::CreateGpio(i, spec);

        // Valve-bank enables start driven low (both banks de-energised) until the valve
        // manager's bring-up raises them.
        const bool safe_ok = !flux::board::IsValveEnablePin(fpin) ||
                             (entry.driver != nullptr && entry.driver->EnsureInitialized() &&
                              entry.driver->SetInactive() == hf_gpio_err_t::GPIO_SUCCESS);

        if (entry.driver != nullptr && entry.driver->EnsureInitialized() && safe_ok) {
            entry.registered = true;
            ++registered_count_;
            Logger::GetInstance().Info(TAG, "  %-28.*s GPIO%-3d %-7s",
                                      static_cast<int>(mapping.name.size()), mapping.name.data(),
                                      mapping.physical_pin, "OK");
        } else {
            flux::bsp::DestroyGpio(i);
            entry.driver = nullptr;
            Logger::GetInstance().Error(TAG, "  %-28.*s GPIO%-3d %-7s",
                                       static_cast<int>(mapping.name.size()), mapping.name.data(),
                                       mapping.physical_pin, "FAIL");
        }
    }

    logger_decor::Rule(TAG);
    Logger::GetInstance().Info(TAG, "GPIO manager initialized: %zu pins registered", registered_count_);
    // Fail closed: every GPIO-category pin in the pin map must be up.
    if (registered_count_ != expected || expected == 0) {
        manager_error_.store(flux::hal::ManagerError::BindFailed, std::memory_order_release);
        Logger::GetInstance().Error(TAG, "GPIO manager NOT initialised (fail-closed): %zu of %zu pins registered",
                                    registered_count_, expected);
        return false;
    }
    return true;
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
    if (!initialized_.load(std::memory_order_acquire)) return nullptr;
    size_t idx = FindByName(name);
    if (idx == kInvalidIndex) return nullptr;
    return entries_[idx].driver;
}

BaseGpio* GpioManager::Get(HfFunctionalGpioPin pin) noexcept {
    if (!initialized_.load(std::memory_order_acquire)) return nullptr;
    size_t idx = static_cast<size_t>(pin);
    if (idx >= kMaxEntries || !entries_[idx].registered) return nullptr;
    return entries_[idx].driver;
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
    size_t idx = FindByName(name);
    if (idx == kInvalidIndex) { ++failed_ops_; return hf_gpio_err_t::GPIO_ERR_PIN_NOT_FOUND; }
    auto& e = entries_[idx];
    hf_gpio_err_t r = value ? e.driver->SetActive() : e.driver->SetInactive();
    ++total_ops_;
    if (r == hf_gpio_err_t::GPIO_SUCCESS) { ++successful_ops_; ++e.access_count; }
    else { ++failed_ops_; ++e.error_count; last_error_ = r; }
    return r;
}

hf_gpio_err_t GpioManager::SetActive(std::string_view name) noexcept {
    size_t idx = FindByName(name);
    if (idx == kInvalidIndex) { ++failed_ops_; return hf_gpio_err_t::GPIO_ERR_PIN_NOT_FOUND; }
    auto& e = entries_[idx];
    hf_gpio_err_t r = e.driver->SetActive();
    ++total_ops_;
    if (r == hf_gpio_err_t::GPIO_SUCCESS) { ++successful_ops_; ++e.access_count; }
    else { ++failed_ops_; ++e.error_count; last_error_ = r; }
    return r;
}

hf_gpio_err_t GpioManager::SetInactive(std::string_view name) noexcept {
    size_t idx = FindByName(name);
    if (idx == kInvalidIndex) { ++failed_ops_; return hf_gpio_err_t::GPIO_ERR_PIN_NOT_FOUND; }
    auto& e = entries_[idx];
    hf_gpio_err_t r = e.driver->SetInactive();
    ++total_ops_;
    if (r == hf_gpio_err_t::GPIO_SUCCESS) { ++successful_ops_; ++e.access_count; }
    else { ++failed_ops_; ++e.error_count; last_error_ = r; }
    return r;
}

hf_gpio_err_t GpioManager::Read(std::string_view name, bool& state) noexcept {
    size_t idx = FindByName(name);
    if (idx == kInvalidIndex) { ++failed_ops_; return hf_gpio_err_t::GPIO_ERR_PIN_NOT_FOUND; }
    auto& e = entries_[idx];
    hf_gpio_err_t r = e.driver->IsActive(state);
    ++total_ops_;
    if (r == hf_gpio_err_t::GPIO_SUCCESS) { ++successful_ops_; ++e.access_count; }
    else { ++failed_ops_; ++e.error_count; last_error_ = r; }
    return r;
}

hf_gpio_err_t GpioManager::Toggle(std::string_view name) noexcept {
    size_t idx = FindByName(name);
    if (idx == kInvalidIndex) { ++failed_ops_; return hf_gpio_err_t::GPIO_ERR_PIN_NOT_FOUND; }
    auto& e = entries_[idx];
    hf_gpio_err_t r = e.driver->Toggle();
    ++total_ops_;
    if (r == hf_gpio_err_t::GPIO_SUCCESS) { ++successful_ops_; ++e.access_count; }
    else { ++failed_ops_; ++e.error_count; last_error_ = r; }
    return r;
}

hf_gpio_err_t GpioManager::IsActive(std::string_view name, bool& active) noexcept {
    return Read(name, active);
}

//==============================================================================
// PIN CONFIGURATION
//==============================================================================

hf_gpio_err_t GpioManager::SetDirection(std::string_view name, hf_gpio_direction_t direction) noexcept {
    auto* gpio = Get(name);
    if (!gpio) return hf_gpio_err_t::GPIO_ERR_PIN_NOT_FOUND;
    return gpio->SetDirection(direction);
}

hf_gpio_err_t GpioManager::SetPullMode(std::string_view name, hf_gpio_pull_mode_t pull_mode) noexcept {
    auto* gpio = Get(name);
    if (!gpio) return hf_gpio_err_t::GPIO_ERR_PIN_NOT_FOUND;
    return gpio->SetPullMode(pull_mode);
}

hf_gpio_err_t GpioManager::SetOutputMode(std::string_view name, hf_gpio_output_mode_t mode) noexcept {
    auto* gpio = Get(name);
    if (!gpio) return hf_gpio_err_t::GPIO_ERR_PIN_NOT_FOUND;
    return gpio->SetOutputMode(mode);
}

//==============================================================================
// DIAGNOSTICS
//==============================================================================

hf_gpio_err_t GpioManager::GetSystemDiagnostics(GpioSystemDiagnostics& diag) const noexcept {
    diag.system_healthy = initialized_.load(std::memory_order_acquire);
    diag.total_pins_registered = static_cast<uint32_t>(registered_count_);
    diag.total_operations = total_ops_;
    diag.successful_operations = successful_ops_;
    diag.failed_operations = failed_ops_;
    diag.system_uptime_ms = (init_time_ms_ > 0) ? (os_get_elapsed_time_msec() - init_time_ms_) : 0;
    diag.last_error = last_error_;
    return hf_gpio_err_t::GPIO_SUCCESS;
}

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

//==============================================================================
// DEINITIALIZE
//==============================================================================

bool GpioManager::Deinitialize() noexcept {
    MutexLockGuard lock(mutex_);
    if (initialized_.load(std::memory_order_acquire)) {
        Logger::GetInstance().Info(TAG, "Deinitializing Flux GPIO manager");
    }
    ReleaseLocked();
    total_ops_ = 0;
    successful_ops_ = 0;
    failed_ops_ = 0;
    last_error_ = hf_gpio_err_t::GPIO_SUCCESS;

    initialized_.store(false, std::memory_order_release);
    manager_error_.store(flux::hal::ManagerError::NotInitialized, std::memory_order_release);
    return true;
}

void GpioManager::ReleaseLocked() noexcept {
    for (size_t i = 0; i < HF_GPIO_MAPPING_SIZE && i < kMaxEntries; ++i) {
        flux::bsp::DestroyGpio(i);
        entries_[i].driver = nullptr;
        entries_[i].registered = false;
        entries_[i].access_count = 0;
        entries_[i].error_count = 0;
    }
    registered_count_ = 0;
}

//==============================================================================
// INTERRUPT SUPPORT (MCU GPIOs — basic support)
//==============================================================================

hf_gpio_err_t GpioManager::ConfigureInterrupt(std::string_view name,
                                               hf_gpio_interrupt_trigger_t trigger,
                                               InterruptCallback callback,
                                               void* user_data) noexcept {
    auto* gpio = Get(name);
    if (!gpio) return hf_gpio_err_t::GPIO_ERR_PIN_NOT_FOUND;
    return gpio->ConfigureInterrupt(trigger, callback, user_data);
}

hf_gpio_err_t GpioManager::EnableInterrupt(std::string_view name) noexcept {
    auto* gpio = Get(name);
    if (!gpio) return hf_gpio_err_t::GPIO_ERR_PIN_NOT_FOUND;
    return gpio->EnableInterrupt();
}

hf_gpio_err_t GpioManager::DisableInterrupt(std::string_view name) noexcept {
    auto* gpio = Get(name);
    if (!gpio) return hf_gpio_err_t::GPIO_ERR_PIN_NOT_FOUND;
    return gpio->DisableInterrupt();
}

bool GpioManager::SupportsInterrupts(std::string_view name) const noexcept {
    // All MCU GPIOs support interrupts
    return FindByName(name) != kInvalidIndex;
}
