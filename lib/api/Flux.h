/**
 * @file Flux.h
 * @brief Unified API singleton for the HardFOC Flux V1 platform.
 *
 * @details Provides a single entry point to every subsystem on the Flux board:
 *
 *   - **comms**   — SPI (MAX22200 + TLE92466ED), I2C, CAN
 *   - **gpio**    — ESP32-C6 GPIO management (control pins for both drivers)
 *   - **valves**  — Unified valve control (solenoid + proportional)
 *   - **adc**     — ESP32 ADC (NTC thermistor, VBUS monitor)
 *   - **leds**    — Single WS2812 status LED
 *   - **temp**    — Temperature (ESP32 internal + NTC thermistor)
 *
 * Initialization Order (dependency-based):
 *   1. CommChannelsManager  (foundation — SPI, I2C, CAN buses)
 *   2. GpioManager          (depends on comms for future expansion)
 *   3. ValveManager         (depends on comms + gpio for drivers)
 *   4. AdcManager           (ESP32 internal ADC, independent)
 *   5. LedManager           (WS2812, independent)
 *   6. TemperatureManager   (depends on ADC)
 *
 * Usage:
 * @code
 *   auto& flux = Flux::GetInstance();
 *   if (flux.EnsureInitialized()) {
 *       flux.valves.EnableChannel(ValveChannel::SOL_CH0);
 *       float ntc_temp;
 *       flux.temp.ReadNtcTemperature(ntc_temp);
 *       flux.leds.IndicateReady();
 *   }
 * @endcode
 *
 * @author HardFOC Team
 * @date 2026
 * @version 1.0
 */

#ifndef FLUX_API_H_
#define FLUX_API_H_

#include <array>
#include <atomic>
#include <cstring>

#include "FluxVersion.h"
#include "handlers/logger/Logger.h"

// Forward declarations
class CommChannelsManager;
class GpioManager;
class ValveManager;
class AdcManager;
class LedManager;
class TemperatureManager;

//==============================================================================
// FLUX SYSTEM DIAGNOSTICS
//==============================================================================

struct FluxSystemDiagnostics {
    bool system_healthy;
    bool comms_initialized;
    bool gpio_initialized;
    bool valves_initialized;
    bool adc_initialized;
    bool leds_initialized;
    bool temp_initialized;

    uint32_t total_components;
    uint32_t initialized_components;
    uint32_t failed_components;

    uint64_t initialization_time_ms;
    uint64_t system_uptime_ms;

    static constexpr size_t kMaxDiagEntries = 6;
    const char* failed_components_list[kMaxDiagEntries]{};
    size_t failed_components_list_count{0};

    FluxSystemDiagnostics() noexcept
        : system_healthy(false), comms_initialized(false), gpio_initialized(false),
          valves_initialized(false), adc_initialized(false), leds_initialized(false),
          temp_initialized(false), total_components(6), initialized_components(0),
          failed_components(0), initialization_time_ms(0), system_uptime_ms(0) {}
};

//==============================================================================
// MANAGER HEALTH SNAPSHOT
//==============================================================================

/**
 * @brief Per-manager health entry for unified error aggregation
 */
struct ManagerHealthEntry {
    const char* name;           ///< Manager name (string literal)
    bool initialized;           ///< Whether manager is initialized
    bool has_error;             ///< Whether last error is non-zero
    uint8_t last_error_code;    ///< Last error code (0 = success, enum-specific otherwise)

    ManagerHealthEntry() noexcept
        : name(nullptr), initialized(false), has_error(false), last_error_code(0) {}
};

/**
 * @brief Aggregated health snapshot across all Flux managers
 *
 * @details Provides a unified view of errors across all component managers.
 *          Error codes are cast to uint8_t; interpretation requires knowledge
 *          of the originating manager's error enum.
 *
 * Supported error sources per manager:
 *   CommChannels  — CommError via GetLastError()
 *   GPIO          — init-status only (no public error accessor)
 *   Valves        — init-status only (no public error accessor)
 *   ADC           — init-status only (no public error accessor)
 *   LEDs          — init-status only (no public error accessor)
 *   Temperature   — init-status only (no public error accessor)
 */
struct ManagerHealthSnapshot {
    static constexpr size_t kMaxManagers = 6;

    ManagerHealthEntry entries[kMaxManagers];    ///< Per-manager entries
    size_t count;                                ///< Number of populated entries
    size_t managers_with_errors;                 ///< Count of managers reporting errors
    bool all_healthy;                            ///< True if no errors and all initialized

    ManagerHealthSnapshot() noexcept
        : entries{}, count(0), managers_with_errors(0), all_healthy(true) {}
};

//==============================================================================
// FLUX API CLASS
//==============================================================================

class Flux {
public:
    //==========================================================================
    // SINGLETON ACCESS
    //==========================================================================

    static Flux& GetInstance() noexcept;

    Flux(const Flux&) = delete;
    Flux& operator=(const Flux&) = delete;
    Flux(Flux&&) = delete;
    Flux& operator=(Flux&&) = delete;

    //==========================================================================
    // INITIALIZATION
    //==========================================================================

    /**
     * @brief Ensure all Flux subsystems are properly initialized.
     * @return true if all critical systems initialized, false otherwise
     */
    bool EnsureInitialized() noexcept;

    [[nodiscard]] bool IsInitialized() const noexcept { return initialized_; }

    [[nodiscard]] bool GetSystemDiagnostics(FluxSystemDiagnostics& diagnostics) const noexcept;
    [[nodiscard]] std::array<bool, 6> GetComponentInitializationStatus() const noexcept;

    /**
     * @brief Collect per-manager error health into a unified snapshot
     * @param snapshot Reference to snapshot structure to fill
     * @return true if snapshot collected successfully, false if not initialized
     * @note Error codes are manager-specific uint8_t values (0 = success)
     */
    [[nodiscard]] bool CollectManagerHealth(ManagerHealthSnapshot& snapshot) noexcept;

    //==========================================================================
    // COMPONENT ACCESS (public references to singletons)
    //==========================================================================

    CommChannelsManager& comms;     ///< SPI, I2C, CAN buses
    GpioManager&         gpio;      ///< ESP32-C6 GPIO pins
    ValveManager&        valves;    ///< Solenoid + proportional valve control
    AdcManager&          adc;       ///< ADC channels (NTC, VBUS)
    LedManager&          leds;      ///< WS2812 status LED
    TemperatureManager&  temp;      ///< Temperature sensors

    //==========================================================================
    // UTILITY
    //==========================================================================

    [[nodiscard]] uint64_t GetSystemUptimeMs() const noexcept;
    [[nodiscard]] uint64_t GetInitializationTimeMs() const noexcept;
    void DumpSystemStatistics() const noexcept;
    [[nodiscard]] bool PerformHealthCheck() noexcept;
    [[nodiscard]] const char* GetSystemVersion() const noexcept;

private:
    Flux() noexcept;
    ~Flux() = default;

    bool InitializeAllComponents() noexcept;
    bool InitializeComms() noexcept;
    bool InitializeGpio() noexcept;
    bool InitializeValves() noexcept;
    bool InitializeAdc() noexcept;
    bool InitializeLeds() noexcept;
    bool InitializeTemperature() noexcept;
    void UpdateSystemDiagnostics() noexcept;

    //==========================================================================
    // STATE
    //==========================================================================

    std::atomic<bool> initialized_{false};
    std::atomic<uint64_t> initialization_start_time_{0};
    std::atomic<uint64_t> initialization_end_time_{0};

    std::atomic<bool> comms_initialized_{false};
    std::atomic<bool> gpio_initialized_{false};
    std::atomic<bool> valves_initialized_{false};
    std::atomic<bool> adc_initialized_{false};
    std::atomic<bool> leds_initialized_{false};
    std::atomic<bool> temp_initialized_{false};

    mutable FluxSystemDiagnostics diagnostics_;

    static constexpr size_t kMaxFailedComponents = 6;
    const char* failed_components_[kMaxFailedComponents]{};
    size_t failed_components_count_{0};

};

//==============================================================================
// GLOBAL ACCESS MACROS
//==============================================================================

#define FLUX_API       Flux::GetInstance()
#define FLUX_API_INIT() (Flux::GetInstance().EnsureInitialized(), Flux::GetInstance())

#endif // FLUX_API_H_
