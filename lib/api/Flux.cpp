/**
 * @file Flux.cpp
 * @brief Implementation of the unified Flux API singleton.
 *
 * @author HardFOC Team
 * @date 2026
 * @version 1.0
 */

#include "Flux.h"

#include "managers/CommChannelsManager.h"
#include "managers/GpioManager.h"
#include "managers/ValveManager.h"
#include "managers/AdcManager.h"
#include "managers/LedManager.h"
#include "managers/TemperatureManager.h"

#include "core/hf-core-utils/hf-utils-rtos-wrap/include/OsUtility.h"

static constexpr const char* TAG = "Flux";

//==============================================================================
// SINGLETON
//==============================================================================

Flux& Flux::GetInstance() noexcept {
    static Flux instance;
    return instance;
}

//==============================================================================
// CONSTRUCTOR
//==============================================================================

Flux::Flux() noexcept
    : comms(CommChannelsManager::GetInstance()),
      gpio(GpioManager::GetInstance()),
      valves(ValveManager::GetInstance()),
      adc(AdcManager::GetInstance()),
      leds(LedManager::GetInstance()),
      temp(TemperatureManager::GetInstance()) {

    Logger::GetInstance().Initialize();
    Logger::GetInstance().Info(TAG, "Flux API singleton created (%s)", FLUX_HAL_VERSION_STRING);
}

//==============================================================================
// INITIALIZATION
//==============================================================================

bool Flux::EnsureInitialized() noexcept {
    if (initialized_) return true;

    Logger::GetInstance().Info(TAG, "Starting Flux API initialization");
    initialization_start_time_ = static_cast<uint64_t>(os_get_elapsed_time_msec());

    bool success = InitializeAllComponents();

    initialization_end_time_ = static_cast<uint64_t>(os_get_elapsed_time_msec());

    if (success) {
        initialized_ = true;
        UpdateSystemDiagnostics();

        uint64_t dt = GetInitializationTimeMs();
        Logger::GetInstance().Info(TAG, "Flux API initialized in %llu ms", dt);
        Logger::GetInstance().Info(TAG, "  Comms=%s GPIO=%s Valves=%s ADC=%s LEDs=%s Temp=%s",
            comms_initialized_  ? "OK" : "FAIL",
            gpio_initialized_   ? "OK" : "FAIL",
            valves_initialized_ ? "OK" : "FAIL",
            adc_initialized_    ? "OK" : "FAIL",
            leds_initialized_   ? "OK" : "FAIL",
            temp_initialized_   ? "OK" : "FAIL");
    } else {
        Logger::GetInstance().Error(TAG, "Flux API initialization failed");
    }

    return success;
}

bool Flux::InitializeAllComponents() noexcept {
    Logger::GetInstance().Info(TAG, "Initializing Flux components in dependency order");

    // Step 1: Communication buses (must be first)
    if (!InitializeComms()) {
        Logger::GetInstance().Error(TAG, "CommChannels failed — aborting");
        return false;
    }

    // Step 2: GPIO (depends on comms for future expansion)
    if (!InitializeGpio()) {
        Logger::GetInstance().Error(TAG, "GPIO failed — aborting");
        return false;
    }

    // Step 3: Valves (depends on comms + gpio)
    if (!InitializeValves()) {
        Logger::GetInstance().Warn(TAG, "Valve init failed — continuing");
    }

    // Step 4: ADC (independent)
    if (!InitializeAdc()) {
        Logger::GetInstance().Warn(TAG, "ADC init failed — continuing");
    }

    // Step 5: LEDs (independent)
    if (!InitializeLeds()) {
        Logger::GetInstance().Warn(TAG, "LED init failed — continuing");
    }

    // Step 6: Temperature (depends on ADC)
    if (!InitializeTemperature()) {
        Logger::GetInstance().Warn(TAG, "Temperature init failed — continuing");
    }

    Logger::GetInstance().Info(TAG, "Component initialization complete");
    return true;
}

bool Flux::InitializeComms() noexcept {
    bool ok = comms.EnsureInitialized();
    comms_initialized_ = ok;
    if (!ok && failed_components_count_ < kMaxFailedComponents)
        failed_components_[failed_components_count_++] = "CommChannelsManager";
    return ok;
}

bool Flux::InitializeGpio() noexcept {
    bool ok = gpio.EnsureInitialized();
    gpio_initialized_ = ok;
    if (!ok && failed_components_count_ < kMaxFailedComponents)
        failed_components_[failed_components_count_++] = "GpioManager";
    return ok;
}

bool Flux::InitializeValves() noexcept {
    bool ok = valves.EnsureInitialized();
    valves_initialized_ = ok;
    if (!ok && failed_components_count_ < kMaxFailedComponents)
        failed_components_[failed_components_count_++] = "ValveManager";
    return ok;
}

bool Flux::InitializeAdc() noexcept {
    bool ok = adc.EnsureInitialized();
    adc_initialized_ = ok;
    if (!ok && failed_components_count_ < kMaxFailedComponents)
        failed_components_[failed_components_count_++] = "AdcManager";
    return ok;
}

bool Flux::InitializeLeds() noexcept {
    bool ok = leds.EnsureInitialized();
    leds_initialized_ = ok;
    if (!ok && failed_components_count_ < kMaxFailedComponents)
        failed_components_[failed_components_count_++] = "LedManager";
    return ok;
}

bool Flux::InitializeTemperature() noexcept {
    bool ok = temp.EnsureInitialized();
    temp_initialized_ = ok;
    if (!ok && failed_components_count_ < kMaxFailedComponents)
        failed_components_[failed_components_count_++] = "TemperatureManager";
    return ok;
}

//==============================================================================
// DIAGNOSTICS
//==============================================================================

bool Flux::GetSystemDiagnostics(FluxSystemDiagnostics& d) const noexcept {
    if (!initialized_) return false;
    const_cast<Flux*>(this)->UpdateSystemDiagnostics();
    d = diagnostics_;
    return true;
}

std::array<bool, 6> Flux::GetComponentInitializationStatus() const noexcept {
    return {{
        comms_initialized_.load(),
        gpio_initialized_.load(),
        valves_initialized_.load(),
        adc_initialized_.load(),
        leds_initialized_.load(),
        temp_initialized_.load()
    }};
}

uint64_t Flux::GetSystemUptimeMs() const noexcept {
    return static_cast<uint64_t>(os_get_elapsed_time_msec());
}

uint64_t Flux::GetInitializationTimeMs() const noexcept {
    uint64_t s = initialization_start_time_.load();
    uint64_t e = initialization_end_time_.load();
    return (s && e && e >= s) ? (e - s) : 0;
}

void Flux::DumpSystemStatistics() const noexcept {
    if (!initialized_) {
        Logger::GetInstance().Warn(TAG, "Not initialized — cannot dump stats");
        return;
    }

    Logger::GetInstance().Info(TAG, "=== Flux System Statistics ===");
    Logger::GetInstance().Info(TAG, "Version: %s  Board: %s  MCU: %s",
                              FLUX_HAL_VERSION_STRING, FLUX_HAL_BOARD_NAME, FLUX_HAL_MCU);
    Logger::GetInstance().Info(TAG, "Uptime: %llu ms  Init time: %llu ms",
                              GetSystemUptimeMs(), GetInitializationTimeMs());

    comms.DumpStatistics();
    gpio.DumpStatistics();
    valves.DumpStatistics();
    adc.DumpStatistics();
    leds.DumpStatistics();
    temp.DumpStatistics();

    Logger::GetInstance().Info(TAG, "=== End Flux System Statistics ===");
}

bool Flux::PerformHealthCheck() noexcept {
    if (!initialized_) return false;

    bool healthy = comms_initialized_ && gpio_initialized_ && valves_initialized_ &&
                   adc_initialized_ && leds_initialized_ && temp_initialized_;

    Logger::GetInstance().Info(TAG, "Health: %s  Comms=%s GPIO=%s Valves=%s ADC=%s LEDs=%s Temp=%s",
        healthy ? "OK" : "DEGRADED",
        comms_initialized_  ? "OK" : "FAIL",
        gpio_initialized_   ? "OK" : "FAIL",
        valves_initialized_ ? "OK" : "FAIL",
        adc_initialized_    ? "OK" : "FAIL",
        leds_initialized_   ? "OK" : "FAIL",
        temp_initialized_   ? "OK" : "FAIL");

    return healthy;
}

const char* Flux::GetSystemVersion() const noexcept {
    return "Flux API v" FLUX_HAL_VERSION_STRING " - " FLUX_HAL_BOARD_NAME;
}

void Flux::UpdateSystemDiagnostics() noexcept {
    diagnostics_.comms_initialized = comms_initialized_;
    diagnostics_.gpio_initialized  = gpio_initialized_;
    diagnostics_.valves_initialized = valves_initialized_;
    diagnostics_.adc_initialized   = adc_initialized_;
    diagnostics_.leds_initialized  = leds_initialized_;
    diagnostics_.temp_initialized  = temp_initialized_;

    diagnostics_.initialized_components = 0;
    if (comms_initialized_)  ++diagnostics_.initialized_components;
    if (gpio_initialized_)   ++diagnostics_.initialized_components;
    if (valves_initialized_) ++diagnostics_.initialized_components;
    if (adc_initialized_)    ++diagnostics_.initialized_components;
    if (leds_initialized_)   ++diagnostics_.initialized_components;
    if (temp_initialized_)   ++diagnostics_.initialized_components;

    diagnostics_.failed_components = diagnostics_.total_components - diagnostics_.initialized_components;
    diagnostics_.initialization_time_ms = GetInitializationTimeMs();
    diagnostics_.system_uptime_ms = GetSystemUptimeMs();
    diagnostics_.system_healthy = (diagnostics_.failed_components == 0) && initialized_;

    diagnostics_.failed_components_list_count = failed_components_count_;
    for (size_t i = 0; i < failed_components_count_ && i < FluxSystemDiagnostics::kMaxDiagEntries; ++i) {
        diagnostics_.failed_components_list[i] = failed_components_[i];
    }
}
