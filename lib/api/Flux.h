/**
 * @file Flux.h
 * @brief Unified API singleton for the HardFOC Flux V1 platform.
 *
 * @details Provides a single entry point to every subsystem on the Flux board:
 *
 *   - **comms**   — SPI (MAX22200 + TLE92466ED), CAN, lazy UART pool
 *   - **gpio**    — ESP32 GPIO management (control pins for both drivers)
 *   - **valves**  — Unified valve control (solenoid + proportional)
 *   - **adc**     — ESP32 ADC (NTC thermistor, VBUS monitor)
 *   - **leds**    — Single WS2812 status LED
 *   - **temp**    — Temperature (ESP32 internal + NTC thermistor)
 *   - **nvs**     — Non-volatile storage (`BaseNvs` / ESP-IDF NVS namespaces)
 *
 * Initialization Order (dependency-based):
 *   1. NvsManager           (flash key-value — before buses so config/cal is available early)
 *   2. CommChannelsManager  (foundation — SPI, CAN, lazy UART acquisition)
 *   3. GpioManager          (depends on comms for future expansion)
 *   4. ValveManager         (depends on comms + gpio for drivers)
 *   5. AdcManager           (ESP32 internal ADC, independent)
 *   6. LedManager           (WS2812, independent)
 *   7. TemperatureManager   (depends on ADC)
 *
 * Task CPU affinity for this board uses logical indices from
 * `FluxCpuAffinity.h` (`flux_hal::kFluxCpuHostIo`, `kFluxCpuRealtime`, …).
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
#include <cstddef>
#include <cstring>

#include "FluxVersion.h"
#include "flux_hal/flux_hal_types.hpp"
#include "handlers/logger/Logger.h"

// Forward declarations
class CommChannelsManager;
class NvsManager;
class GpioManager;
class ValveManager;
class AdcManager;
class LedManager;
class TemperatureManager;

/** @brief Number of Flux subsystem managers exposed on `Flux` (incl. NVS). */
inline constexpr size_t kFluxManagerCount = 7;

//==============================================================================
// FLUX SYSTEM DIAGNOSTICS
//==============================================================================

/** @brief Complete system diagnostics snapshot for the Flux API. */
struct FluxSystemDiagnostics {
    bool system_healthy;           ///< true when all components are initialised.
    bool nvs_initialized;           ///< NvsManager ready.
    bool comms_initialized;        ///< CommChannelsManager ready.
    bool gpio_initialized;         ///< GpioManager ready.
    bool valves_initialized;       ///< ValveManager ready.
    bool adc_initialized;          ///< AdcManager ready.
    bool leds_initialized;         ///< LedManager ready.
    bool temp_initialized;         ///< TemperatureManager ready.

    uint32_t total_components;         ///< Total number of managed components (`kFluxManagerCount`).
    uint32_t initialized_components;   ///< Components that initialised successfully.
    uint32_t failed_components;        ///< Components that failed to initialise.

    uint64_t initialization_time_ms;   ///< Time taken to initialise all components.
    uint64_t system_uptime_ms;         ///< Milliseconds since first initialisation.

    static constexpr size_t kMaxDiagEntries = kFluxManagerCount;  ///< Max failed-component names.
    const char* failed_components_list[kMaxDiagEntries]{}; ///< Names of failed components.
    size_t failed_components_list_count{0};                ///< Count of entries in list.

    /** @brief Default constructor — all fields zeroed / false. */
    FluxSystemDiagnostics() noexcept
        : system_healthy(false), nvs_initialized(false), comms_initialized(false), gpio_initialized(false),
          valves_initialized(false), adc_initialized(false), leds_initialized(false),
          temp_initialized(false), total_components(static_cast<uint32_t>(kFluxManagerCount)),
          initialized_components(0),
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
 *   NVS           — hf_nvs_err_t via GetLastError()
 *   CommChannels  — CommError via GetLastError()
 *   GPIO          — hf_gpio_err_t via GetLastError()
 *   Valves        — hf_valve_err_t via GetLastError()
 *   ADC           — hf_adc_err_t via GetLastError()
 *   LEDs          — LedError via GetLastError()
 *   Temperature   — TemperatureError via GetLastError()
 */
struct ManagerHealthSnapshot {
    static constexpr size_t kMaxManagers = kFluxManagerCount;

    ManagerHealthEntry entries[kMaxManagers];    ///< Per-manager entries
    size_t count;                                ///< Number of populated entries
    size_t managers_with_errors;                 ///< Count of managers reporting errors
    bool all_healthy;                            ///< True if no errors and all initialized

    ManagerHealthSnapshot() noexcept
        : entries{}, count(0), managers_with_errors(0), all_healthy(true) {}
};

//==============================================================================
// AGGREGATED DIAGNOSTICS (shared ManagerError catalog)
//==============================================================================

/** @brief One manager's lifecycle state in @ref FluxHalDiagnostics. */
struct ManagerDiagnostic {
    const char* name{nullptr};                                     ///< Manager class name
    bool required{false};                                          ///< Its failure fails Flux::EnsureInitialized()
    bool initialized{false};                                       ///< IsInitialized()
    flux::hal::ManagerError error{flux::hal::ManagerError::NotInitialized};  ///< GetManagerError()
};

/**
 * @brief Board-level bring-up snapshot: every manager's state and every fitted part's readiness.
 * @details Filled by @ref Flux::CollectDiagnostics. Managers fail closed, so a part that did not
 *          come up shows here as not ready and its manager as not initialised with a reason; the
 *          part flags are valid even when their manager failed (they say which part is missing).
 */
struct FluxHalDiagnostics {
    bool initialized{false};       ///< Flux::EnsureInitialized() succeeded (every required manager up)
    bool all_managers_up{false};   ///< Every manager, required and optional, is initialised
    bool all_healthy{false};       ///< ... and none reports an error (none degraded)
    flux::hal::ManagerError first_error{flux::hal::ManagerError::None};  ///< First error in init order
    const char* first_error_manager{nullptr};                      ///< Manager that reported it

    std::array<ManagerDiagnostic, kFluxManagerCount> managers{};   ///< In initialisation order

    bool max22200_ready{false};      ///< MAX22200 solenoid driver came up
    bool tle92466_ready{false};      ///< TLE92466ED proportional driver came up
    bool pressure_bus_ready{false};  ///< ADS7952 SPI bus and chip select are up
    bool ads7952_ready{false};       ///< ADS7952 device 0 came up
    bool internal_adc_ready{false};  ///< MCU ADC (NTC, VBUS) came up
    bool can_ready{false};           ///< CAN controller object exists
    bool die_temp_ready{false};      ///< MCU die sensor came up
    bool ntc_ready{false};           ///< Board NTC came up
    bool mcp9700_ready{false};       ///< MCP9700 on the ADS7952 came up
    bool status_led_ready{false};    ///< WS2812 status LED came up
};

//==============================================================================
// FLUX API CLASS
//==============================================================================

/**
 * @class Flux
 * @brief Unified singleton entry point for the HardFOC Flux V1 platform.
 *
 * @details Orchestrates initialisation of all subsystem managers (NVS, Comms, GPIO,
 *          Valves, ADC, LED, Temperature) in deterministic order and exposes
 *          them as public reference members for direct access.  Provides
 *          system-level diagnostics, health checking, graceful shutdown, and
 *          runtime warning collection.
 */
class Flux {
public:
    //==========================================================================
    // SINGLETON ACCESS
    //==========================================================================

    /**
     * @brief Access the singleton instance (Meyers singleton, thread-safe).
     * @return Reference to the sole Flux instance.
     */
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

    /** @brief Check whether the system is fully initialised.
     *  @return true if EnsureInitialized() has succeeded. */
    [[nodiscard]] bool IsInitialized() const noexcept { return initialized_; }

    /**
     * @brief Shut down all subsystems in reverse initialisation order.
     * @return true if all subsystems shut down cleanly.
     */
    bool Shutdown() noexcept;

    /**
     * @brief Populate a full system diagnostics snapshot.
     * @param[out] diagnostics  Struct filled with per-component status.
     * @return true on success.
     */
    [[nodiscard]] bool GetSystemDiagnostics(FluxSystemDiagnostics& diagnostics) const noexcept;

    /**
     * @brief Per-component init status as a bool array.
     * @return Array of 7 booleans: [nvs, comms, gpio, valves, adc, leds, temp].
     */
    [[nodiscard]] std::array<bool, kFluxManagerCount> GetComponentInitializationStatus() const noexcept;

    /**
     * @brief Get the list of components that failed initialization.
     * @param out       Output array of const char* pointers (filled with string literals)
     * @param max_count Maximum entries to write
     * @return Number of entries written
     */
    [[nodiscard]] size_t GetFailedComponents(const char* out[], size_t max_count) const noexcept;

    /**
     * @brief Get accumulated system warnings.
     * @param out       Output array of const char* pointers
     * @param max_count Maximum entries to write
     * @return Number of entries written
     */
    [[nodiscard]] size_t GetSystemWarnings(const char* out[], size_t max_count) const noexcept;

    /**
     * @brief Collect per-manager error health into a unified snapshot
     * @param snapshot Reference to snapshot structure to fill
     * @return true if snapshot collected successfully, false if not initialized
     * @note Error codes are manager-specific uint8_t values (0 = success)
     */
    [[nodiscard]] bool CollectManagerHealth(ManagerHealthSnapshot& snapshot) noexcept;

    /**
     * @brief Fill the board-level bring-up snapshot (managers in init order, part readiness).
     * @details Works before, during and after initialisation and never fails closed: it reports
     *          what is up and why the rest is not. Uses the shared @ref flux::hal::ManagerError
     *          catalog, so codes from different managers compare.
     * @param[out] out Overwritten.
     * @return true (always filled).
     */
    bool CollectDiagnostics(FluxHalDiagnostics& out) const noexcept;

    /**
     * @brief Why the last EnsureInitialized() failed: the error of the first required manager
     *        that did not come up, or ManagerError::None.
     */
    [[nodiscard]] flux::hal::ManagerError GetLastError() const noexcept {
        return last_error_.load(std::memory_order_acquire);
    }

    //==========================================================================
    // COMPONENT ACCESS (public references to singletons)
    //==========================================================================

    CommChannelsManager& comms;     ///< SPI, CAN, UART (I²C not used on Flux V1)
    NvsManager&          nvs;       ///< NVS (`flux` / `flux_cal` / `flux_cfg`)
    GpioManager&         gpio;      ///< ESP32 GPIO pins
    ValveManager&        valves;    ///< Solenoid + proportional valve control
    AdcManager&          adc;       ///< ADC channels (NTC, VBUS)
    LedManager&          leds;      ///< WS2812 status LED
    TemperatureManager&  temp;      ///< Temperature sensors

    //==========================================================================
    // UTILITY
    //==========================================================================

    /** @brief Milliseconds elapsed since EnsureInitialized() completed. */
    [[nodiscard]] uint64_t GetSystemUptimeMs() const noexcept;

    /** @brief Time in ms that the initialisation sequence took to complete. */
    [[nodiscard]] uint64_t GetInitializationTimeMs() const noexcept;

    //==========================================================================
    // SYSTEM CONTROL (MCU-agnostic wrappers)
    //==========================================================================

    /**
     * @brief Reset reason of the **previous** boot, opaque to callers.
     *
     * Wraps `esp_reset_reason()` on ESP32 today; on other MCUs an
     * equivalent vendor enum will back this. Apps treat the return as
     * an opaque `uint32_t` they can persist to NVS or print verbatim;
     * decoding is platform-specific and lives in HAL utilities.
     *
     * @return Vendor-specific reset cause (`esp_reset_reason_t`-shaped
     *         on ESP32; 0 on the unsupported test stub).
     */
    [[nodiscard]] uint32_t GetLastResetReason() const noexcept;

    /**
     * @brief Free bytes available in the default heap.
     *
     * Wraps `heap_caps_get_free_size(MALLOC_CAP_DEFAULT)` on ESP32.
     * Useful for diagnostic logging on thread-start failures (e.g.
     * `xTaskCreate` returning false often correlates with heap
     * exhaustion). Returns 0 on the unsupported test stub.
     */
    [[nodiscard]] uint32_t GetFreeHeapBytes() const noexcept;

    /**
     * @brief Largest contiguous free block in the default heap.
     *
     * Wraps `heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT)` on
     * ESP32. Combined with `GetFreeHeapBytes()` it tells you whether
     * the heap is fragmented (free total >> largest block) or simply
     * exhausted.
     */
    [[nodiscard]] uint32_t GetLargestFreeHeapBlock() const noexcept;

    /**
     * @brief Trigger a software reset of the MCU. Does not return on
     *        success.
     *
     * Wraps `esp_restart()` on ESP32. Apps call this from supervisor
     * paths (e.g. `app_main`'s recovery loop) when something has gone
     * unrecoverably wrong. The HAL closes/flushes nothing here — the
     * caller is responsible for any pre-reboot logging or NVS commit.
     */
    [[noreturn]] void SystemRestart() const noexcept;

    /** @brief Log a comprehensive system summary to the console. */
    void DumpSystemStatistics() const noexcept;

    /**
     * @brief Run a quick health check across all managers.
     * @return true if every manager reports no errors.
     */
    [[nodiscard]] bool PerformHealthCheck() noexcept;

    /**
     * @brief Get the Flux HAL semantic version string.
     * @return Null-terminated version string (e.g. "1.0.0").
     */
    [[nodiscard]] const char* GetSystemVersion() const noexcept;

private:
    Flux() noexcept;
    ~Flux() = default;

    bool InitializeAllComponents() noexcept;
    bool InitializeNvs() noexcept;
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
    std::atomic<flux::hal::ManagerError> last_error_{flux::hal::ManagerError::None};
    std::atomic<uint64_t> initialization_start_time_{0};
    std::atomic<uint64_t> initialization_end_time_{0};

    std::atomic<bool> nvs_initialized_{false};
    std::atomic<bool> comms_initialized_{false};
    std::atomic<bool> gpio_initialized_{false};
    std::atomic<bool> valves_initialized_{false};
    std::atomic<bool> adc_initialized_{false};
    std::atomic<bool> leds_initialized_{false};
    std::atomic<bool> temp_initialized_{false};

    mutable FluxSystemDiagnostics diagnostics_;

    static constexpr size_t kMaxFailedComponents = kFluxManagerCount;
    const char* failed_components_[kMaxFailedComponents]{};
    size_t failed_components_count_{0};

    static constexpr size_t kMaxSystemWarnings = 16;
    const char* system_warnings_[kMaxSystemWarnings]{};
    size_t system_warnings_count_{0};

    void AddSystemWarning(const char* warning) noexcept {
        if (system_warnings_count_ < kMaxSystemWarnings) {
            system_warnings_[system_warnings_count_++] = warning;
        }
    }
};

//==============================================================================
// CONVENIENCE
//==============================================================================

/**
 * @brief Convenience accessor — equivalent to Flux::GetInstance().
 * @return Reference to the singleton Flux API.
 */
[[nodiscard]] inline Flux& GetFlux() noexcept {
    return Flux::GetInstance();
}

//==============================================================================
// GLOBAL ACCESS MACROS
//==============================================================================

/** @brief Shorthand for Flux::GetInstance(). */
#define FLUX_API       Flux::GetInstance()
/** @brief Initialise (if needed) and return the Flux API instance. */
#define FLUX_API_INIT() (Flux::GetInstance().EnsureInitialized(), Flux::GetInstance())

#endif // FLUX_API_H_
