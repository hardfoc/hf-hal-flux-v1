/**
 * @file Flux.cpp
 * @brief Implementation of the unified Flux API singleton.
 *
 * @author HardFOC Team
 * @date 2026
 * @version 1.0
 */

#include "Flux.h"

#include "managers/NvsManager.h"
#include "managers/CommChannelsManager.h"
#include "managers/GpioManager.h"
#include "managers/ValveManager.h"
#include "managers/AdcManager.h"
#include "managers/LedManager.h"
#include "managers/TemperatureManager.h"

#include "core/hf-core-utils/hf-utils-rtos-wrap/include/OsUtility.h"
#include "core/handlers/logger/LogDecorators.h"

#include "managers/bsp_bind/FluxPlatformBind.h"

static constexpr const char* TAG = "Flux";

namespace {

void LogInitHeader() noexcept {
    logger_decor::Banner(TAG, "Flux HAL bring-up");
    Logger::GetInstance().Info(TAG, "  %-2s %-15s %-7s %s", "#", "Component", "Result", "Elapsed");
    Logger::GetInstance().Info(TAG, "  %-2s %-15s %-7s %s", "--", "---------", "------", "-------");
}

void LogInitRow(unsigned step,
                const char* name,
                bool ok,
                bool fatal,
                uint32_t elapsed_ms) noexcept {
    const char* result = ok ? "OK" : (fatal ? "FAIL" : "WARN");
    if (ok) {
        Logger::GetInstance().Info(TAG,
            "  %-2u %-15s %-7s %5lu ms", step, name, result,
            static_cast<unsigned long>(elapsed_ms));
        return;
    }
    Logger::GetInstance().Warn(TAG,
        "  %-2u %-15s %-7s %5lu ms", step, name, result,
        static_cast<unsigned long>(elapsed_ms));
}

void LogInitFooter() noexcept {
    logger_decor::Rule(TAG);
}

void LogStepBanner(unsigned step, const char* name) noexcept {
    char title[64];
    std::snprintf(title, sizeof(title), "[%u/7] %s", step, name);
    logger_decor::Section(TAG, title);
}

}  // namespace

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
      nvs(NvsManager::GetInstance()),
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
        last_error_.store(flux::hal::ManagerError::None, std::memory_order_release);
        UpdateSystemDiagnostics();

        uint64_t dt = GetInitializationTimeMs();
        Logger::GetInstance().Info(TAG, "Flux API initialized in %lu ms",
            static_cast<unsigned long>(dt));
        Logger::GetInstance().Info(TAG, "  NVS=%s Comms=%s GPIO=%s Valves=%s ADC=%s LEDs=%s Temp=%s",
            nvs_initialized_    ? "OK" : "FAIL",
            comms_initialized_  ? "OK" : "FAIL",
            gpio_initialized_   ? "OK" : "FAIL",
            valves_initialized_ ? "OK" : "FAIL",
            adc_initialized_    ? "OK" : "FAIL",
            leds_initialized_   ? "OK" : "FAIL",
            temp_initialized_   ? "OK" : "FAIL");
    } else {
        FluxHalDiagnostics d{};
        (void)CollectDiagnostics(d);
        last_error_.store(d.first_error, std::memory_order_release);
        Logger::GetInstance().Error(TAG, "Flux API initialization failed: %s (%s)",
                                    d.first_error_manager != nullptr ? d.first_error_manager : "?",
                                    flux::hal::ToString(d.first_error));
    }

    return success;
}

bool Flux::InitializeAllComponents() noexcept {
    Logger::GetInstance().Info(TAG, "Initializing Flux components in dependency order");
    LogInitHeader();

    auto run_step = [](unsigned step,
                       const char* name,
                       bool fatal,
                       bool (*fn)(Flux*)) noexcept -> bool {
        LogStepBanner(step, name);
        const uint64_t t0 = static_cast<uint64_t>(os_get_elapsed_time_msec());
        Flux* self = &Flux::GetInstance();
        const bool ok = fn(self);
        const uint64_t t1 = static_cast<uint64_t>(os_get_elapsed_time_msec());
        const uint32_t dt = static_cast<uint32_t>((t1 >= t0) ? (t1 - t0) : 0U);
        LogInitRow(step, name, ok, fatal, dt);
        if (!ok && fatal) {
            Logger::GetInstance().Error(TAG, "%s failed - aborting", name);
        }
        return ok || !fatal;
    };

    if (!run_step(1, "NVS", true, [](Flux* f) noexcept { return f->InitializeNvs(); })) {
        LogInitFooter();
        return false;
    }
    if (!run_step(2, "Comms", true, [](Flux* f) noexcept { return f->InitializeComms(); })) {
        LogInitFooter();
        return false;
    }
    if (!run_step(3, "GPIO", true, [](Flux* f) noexcept { return f->InitializeGpio(); })) {
        LogInitFooter();
        return false;
    }

    (void)run_step(4, "Valves", false, [](Flux* f) noexcept { return f->InitializeValves(); });
    (void)run_step(5, "ADC", false, [](Flux* f) noexcept { return f->InitializeAdc(); });
    (void)run_step(6, "LEDs", false, [](Flux* f) noexcept { return f->InitializeLeds(); });
    (void)run_step(7, "Temp", false, [](Flux* f) noexcept { return f->InitializeTemperature(); });

    LogInitFooter();
    Logger::GetInstance().Info(TAG, "Component initialization complete");
    return true;
}

bool Flux::InitializeNvs() noexcept {
    bool ok = nvs.EnsureInitialized();
    nvs_initialized_ = ok;
    if (!ok && failed_components_count_ < kMaxFailedComponents)
        failed_components_[failed_components_count_++] = "NvsManager";
    return ok;
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

std::array<bool, kFluxManagerCount> Flux::GetComponentInitializationStatus() const noexcept {
    return {{
        nvs_initialized_.load(),
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

//==============================================================================
// SYSTEM CONTROL (MCU-agnostic wrappers around vendor reset / restart APIs)
//==============================================================================

uint32_t Flux::GetLastResetReason() const noexcept {
    return flux::bsp::LastResetReason();
}

void Flux::SystemRestart() const noexcept {
    Logger::GetInstance().Warn(TAG, "Flux::SystemRestart() — rebooting");
    flux::bsp::SystemRestart();
}

uint32_t Flux::GetFreeHeapBytes() const noexcept {
    return flux::bsp::FreeHeapBytes();
}

uint32_t Flux::GetLargestFreeHeapBlock() const noexcept {
    return flux::bsp::LargestFreeHeapBlock();
}

bool Flux::CollectManagerHealth(ManagerHealthSnapshot& snapshot) noexcept {
    if (!initialized_) return false;

    snapshot = ManagerHealthSnapshot{};
    snapshot.count = kFluxManagerCount;
    snapshot.all_healthy = true;
    snapshot.managers_with_errors = 0;

    auto addEntry = [&](size_t idx, const char* name, bool init, uint8_t err) {
        snapshot.entries[idx].name = name;
        snapshot.entries[idx].initialized = init;
        snapshot.entries[idx].last_error_code = err;
        snapshot.entries[idx].has_error = (err != 0);
        if (err != 0) {
            snapshot.all_healthy = false;
            ++snapshot.managers_with_errors;
        }
        if (!init) {
            snapshot.all_healthy = false;
        }
    };

    // 1. NVS — hf_nvs_err_t
    addEntry(0, "NVS", nvs_initialized_.load(),
             static_cast<uint8_t>(nvs.GetLastError()));

    // 2. CommChannelsManager — direct GetLastError()
    addEntry(1, "CommChannels", comms_initialized_.load(),
             static_cast<uint8_t>(comms.GetLastError()));

    // 3. GPIO — GetLastError()
    addEntry(2, "GPIO",        gpio_initialized_.load(),
             static_cast<uint8_t>(gpio.GetLastError()));

    // 4. Valves — GetLastError()
    addEntry(3, "Valves",      valves_initialized_.load(),
             static_cast<uint8_t>(valves.GetLastError()));

    // 5. ADC — GetLastError()
    addEntry(4, "ADC",         adc_initialized_.load(),
             static_cast<uint8_t>(adc.GetLastError()));

    // 6. LEDs — GetLastError()
    addEntry(5, "LEDs",        leds_initialized_.load(),
             static_cast<uint8_t>(leds.GetLastError()));

    // 7. Temperature — GetLastError()
    addEntry(6, "Temperature", temp_initialized_.load(),
             static_cast<uint8_t>(temp.GetLastError()));

    return true;
}

bool Flux::CollectDiagnostics(FluxHalDiagnostics& out) const noexcept {
    out = FluxHalDiagnostics{};
    out.initialized = initialized_.load(std::memory_order_acquire);

    // Same order as InitializeAllComponents(); the first three are required.
    const ManagerDiagnostic rows[kFluxManagerCount] = {
        {"NvsManager", true, nvs.IsInitialized(), nvs.GetManagerError()},
        {"CommChannelsManager", true, comms.IsInitialized(), comms.GetManagerError()},
        {"GpioManager", true, gpio.IsInitialized(), gpio.GetManagerError()},
        {"ValveManager", false, valves.IsInitialized(), valves.GetManagerError()},
        {"AdcManager", false, adc.IsInitialized(), adc.GetManagerError()},
        {"LedManager", false, leds.IsInitialized(), leds.GetManagerError()},
        {"TemperatureManager", false, temp.IsInitialized(), temp.GetManagerError()},
    };
    out.all_managers_up = true;
    out.all_healthy = true;
    for (size_t i = 0; i < kFluxManagerCount; ++i) {
        out.managers[i] = rows[i];
        if (!rows[i].initialized) {
            out.all_managers_up = false;
        }
        if (rows[i].error != flux::hal::ManagerError::None) {
            out.all_healthy = false;
            if (out.first_error == flux::hal::ManagerError::None) {
                // A manager never attempted reports NotInitialized; one that failed (or runs
                // degraded) reports its own reason. The first in init order is the cause.
                out.first_error = rows[i].error;
                out.first_error_manager = rows[i].name;
            }
        }
    }
    out.all_healthy = out.all_healthy && out.all_managers_up;

    out.max22200_ready = valves.IsBankReady(ValveDriverType::MAX22200);
    out.tle92466_ready = valves.IsBankReady(ValveDriverType::TLE92466);
    out.pressure_bus_ready = comms.IsPressureBusReady();
    out.ads7952_ready = adc.IsAds7952Ready(0);
    out.internal_adc_ready = adc.IsInternalAdcReady();
    out.can_ready = comms.IsCanReady();
    out.die_temp_ready = temp.IsSensorReady(FluxTempSensorType::ESP32_INTERNAL);
    out.ntc_ready = temp.IsSensorReady(FluxTempSensorType::NTC_THERMISTOR);
    out.mcp9700_ready = temp.IsSensorReady(FluxTempSensorType::MCP9700_THERMISTOR);
    out.status_led_ready = leds.IsInitialized();
    return true;
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

    nvs.DumpStatistics();
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

    // Collect per-manager error health
    ManagerHealthSnapshot health{};
    (void)CollectManagerHealth(health);

    // Log per-manager status with error codes
    for (size_t i = 0; i < health.count; ++i) {
        const auto& e = health.entries[i];
        if (!e.initialized) {
            Logger::GetInstance().Error(TAG, "  %s: NOT INITIALIZED", e.name);
        } else if (e.has_error) {
            Logger::GetInstance().Warn(TAG, "  %s: ERROR (code %u)", e.name,
                                      static_cast<unsigned>(e.last_error_code));
        } else {
            Logger::GetInstance().Info(TAG, "  %s: OK", e.name);
        }
    }

    Logger::GetInstance().Info(TAG, "Health: %s (%zu/%zu managers healthy)",
        health.all_healthy ? "OK" : "DEGRADED",
        health.count - health.managers_with_errors, health.count);

    return health.all_healthy;
}

const char* Flux::GetSystemVersion() const noexcept {
    return "Flux API v" FLUX_HAL_VERSION_STRING " - " FLUX_HAL_BOARD_NAME;
}

void Flux::UpdateSystemDiagnostics() noexcept {
    diagnostics_.nvs_initialized   = nvs_initialized_;
    diagnostics_.comms_initialized = comms_initialized_;
    diagnostics_.gpio_initialized  = gpio_initialized_;
    diagnostics_.valves_initialized = valves_initialized_;
    diagnostics_.adc_initialized   = adc_initialized_;
    diagnostics_.leds_initialized  = leds_initialized_;
    diagnostics_.temp_initialized  = temp_initialized_;

    diagnostics_.initialized_components = 0;
    if (nvs_initialized_)    ++diagnostics_.initialized_components;
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

//==============================================================================
// SHUTDOWN
//==============================================================================

bool Flux::Shutdown() noexcept {
    if (!initialized_) return true;

    Logger::GetInstance().Info(TAG, "Shutting down Flux API");

    // Reverse initialization order: Temp → LEDs → ADC → Valves → GPIO → Comms → NVS.
    // Valves turn every channel off before their handlers are released.
    (void)temp.Deinitialize();
    (void)leds.Deinitialize();
    (void)adc.Deinitialize();
    (void)valves.Deinitialize();
    (void)gpio.Deinitialize();
    (void)comms.Deinitialize();
    (void)nvs.Deinitialize();

    temp_initialized_   = false;
    leds_initialized_   = false;
    adc_initialized_    = false;
    valves_initialized_ = false;
    gpio_initialized_   = false;
    comms_initialized_  = false;
    nvs_initialized_    = false;
    initialized_        = false;

    Logger::GetInstance().Info(TAG, "Flux API shut down complete");
    return true;
}

//==============================================================================
// FAILED COMPONENTS / WARNINGS
//==============================================================================

size_t Flux::GetFailedComponents(const char* out[], size_t max_count) const noexcept {
    size_t count = (failed_components_count_ < max_count) ? failed_components_count_ : max_count;
    for (size_t i = 0; i < count; ++i) {
        out[i] = failed_components_[i];
    }
    return count;
}

size_t Flux::GetSystemWarnings(const char* out[], size_t max_count) const noexcept {
    size_t count = (system_warnings_count_ < max_count) ? system_warnings_count_ : max_count;
    for (size_t i = 0; i < count; ++i) {
        out[i] = system_warnings_[i];
    }
    return count;
}
