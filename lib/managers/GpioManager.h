/**
 * @file GpioManager.h
 * @brief GPIO management for the HardFOC Flux V1 platform.
 *
 * @details The Flux board uses only ESP32-C6 internal GPIOs (no GPIO expander).
 *          Control pins for MAX22200 and TLE92466ED are managed as standard GPIOs.
 *          This is simpler than the Vortex GpioManager which spans 3 chip types.
 *
 *          Design improvement over Vortex: uses a fixed-size array for GPIO
 *          storage (all pins known at compile time) instead of heap-allocated
 *          unordered_map. String lookups go through a constexpr mapping table.
 *
 * @author HardFOC Team
 * @date 2026
 * @version 1.0
 */

#ifndef FLUX_GPIO_MANAGER_H_
#define FLUX_GPIO_MANAGER_H_

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string_view>

#include "core/hf-core-drivers/internal/hf-internal-interface-wrap/inc/base/BaseGpio.h"
#include "core/hf-core-drivers/internal/hf-internal-interface-wrap/inc/mcu/esp32/EspGpio.h"
#include "RtosMutex.h"
#include "core/hf-core-drivers/internal/hf-pincfg/src/hf_functional_pin_config_flux_v1.hpp"

//==============================================================================
// GPIO ENTRY — owns an EspGpio instance via unique_ptr (heap-allocated)
//==============================================================================

/**
 * @brief A single GPIO entry in the fixed-size registry.
 *
 * Owns an EspGpio instance via unique_ptr (heap-allocated, unique ownership).
 * No shared_ptr. The GpioEntry structs themselves live in a fixed-size
 * std::array inside the singleton (static storage). EspGpio is polymorphic
 * and non-movable, so unique_ptr is needed for the driver instance.
 * The pin count is known at compile time.
 */
struct GpioEntry {
    std::unique_ptr<EspGpio> driver; ///< ESP32 GPIO driver instance (heap-allocated)
    HfFunctionalGpioPin functional_pin; ///< Functional pin enum value
    bool registered{false};             ///< Whether this entry has been registered
    uint32_t access_count{0};           ///< Number of times accessed
    uint32_t error_count{0};            ///< Number of errors

    GpioEntry() noexcept
        : driver(nullptr), functional_pin(HfFunctionalGpioPin::HF_FUNCTIONAL_GPIO_COUNT) {}
};

//==============================================================================
// GPIO SYSTEM DIAGNOSTICS
//==============================================================================

/** @brief System-level GPIO diagnostics snapshot. */
struct GpioSystemDiagnostics {
    bool system_healthy;               ///< true when manager is initialised and operational.
    uint32_t total_pins_registered;    ///< Number of GPIO pins currently registered.
    uint32_t total_operations;         ///< Lifetime count of read/write/toggle calls.
    uint32_t successful_operations;    ///< Calls that returned GPIO_SUCCESS.
    uint32_t failed_operations;        ///< Calls that returned an error.
    uint64_t system_uptime_ms;         ///< Milliseconds since manager initialisation.
    hf_gpio_err_t last_error;          ///< Most recent error code.
};

//==============================================================================
// GPIO MANAGER
//==============================================================================

/**
 * @class GpioManager
 * @brief Singleton that manages all ESP32 GPIO pins defined in the X-MACRO pin config.
 *
 * @details Registers GPIO and USER category pins, provides name-based and
 *          enum-based access to the underlying BaseGpio drivers, and offers
 *          high-level Set/Read/Toggle/Interrupt operations routed by pin name.
 *          Thread-safe via RtosMutex.
 */
class GpioManager {
public:
    //==========================================================================
    // SINGLETON AND LIFECYCLE
    //==========================================================================

    /**
     * @brief Access the singleton instance (Meyers singleton, thread-safe).
     * @return Reference to the sole GpioManager instance.
     */
    static GpioManager& GetInstance() noexcept;

    GpioManager(const GpioManager&) = delete;
    GpioManager& operator=(const GpioManager&) = delete;
    GpioManager(GpioManager&&) = delete;
    GpioManager& operator=(GpioManager&&) = delete;

    /**
     * @brief Register all GPIO pins and initialise drivers (idempotent).
     * @return true on success or if already initialised.
     */
    [[nodiscard]] bool EnsureInitialized() noexcept;

    /** @brief Check whether the manager is initialised.
     *  @return true if ready. */
    [[nodiscard]] bool IsInitialized() const noexcept { return initialized_.load(std::memory_order_acquire); }

    /**
     * @brief Reset GPIO manager to uninitialized state.
     * @return true on success
     */
    [[nodiscard]] bool Deinitialize() noexcept;

    /**
     * @brief Get the most recent error code.
     * @return Last hf_gpio_err_t set by any API call
     */
    [[nodiscard]] hf_gpio_err_t GetLastError() const noexcept { return last_error_; }

    //==========================================================================
    // GPIO ACCESS — by string name
    //==========================================================================

    /** @brief Get the BaseGpio driver for a named pin. Returns nullptr if not found. */
    [[nodiscard]] BaseGpio* Get(std::string_view name) noexcept;

    /** @brief Get the BaseGpio driver for a functional pin enum. Returns nullptr if not found. */
    [[nodiscard]] BaseGpio* Get(HfFunctionalGpioPin pin) noexcept;

    /** @brief Check if a named pin exists in the registry. */
    [[nodiscard]] bool Contains(std::string_view name) const noexcept;

    /** @brief Number of registered GPIO pins. */
    [[nodiscard]] size_t Size() const noexcept;

    //==========================================================================
    // BASIC GPIO OPERATIONS — string-based routing
    //==========================================================================

    /**
     * @brief Set a GPIO output to high (true) or low (false).
     * @param name   Pin name from pincfg.
     * @param value  true = active/high, false = inactive/low.
     * @return GPIO_SUCCESS on success.
     */
    [[nodiscard]] hf_gpio_err_t Set(std::string_view name, bool value) noexcept;

    /**
     * @brief Drive a pin to its active state.
     * @param name  Pin name from pincfg.
     * @return GPIO_SUCCESS on success.
     */
    [[nodiscard]] hf_gpio_err_t SetActive(std::string_view name) noexcept;

    /**
     * @brief Drive a pin to its inactive state.
     * @param name  Pin name from pincfg.
     * @return GPIO_SUCCESS on success.
     */
    [[nodiscard]] hf_gpio_err_t SetInactive(std::string_view name) noexcept;

    /**
     * @brief Read the current digital level of a pin.
     * @param      name   Pin name from pincfg.
     * @param[out] state  true = high, false = low.
     * @return GPIO_SUCCESS on success.
     */
    [[nodiscard]] hf_gpio_err_t Read(std::string_view name, bool& state) noexcept;

    /**
     * @brief Toggle pin output between high and low.
     * @param name  Pin name from pincfg.
     * @return GPIO_SUCCESS on success.
     */
    [[nodiscard]] hf_gpio_err_t Toggle(std::string_view name) noexcept;

    /**
     * @brief Check whether a pin is currently in its active state.
     * @param      name    Pin name from pincfg.
     * @param[out] active  true if pin is active.
     * @return GPIO_SUCCESS on success.
     */
    [[nodiscard]] hf_gpio_err_t IsActive(std::string_view name, bool& active) noexcept;

    //==========================================================================
    // PIN CONFIGURATION
    //==========================================================================

    /**
     * @brief Change pin direction (input / output / open-drain).
     * @param name       Pin name from pincfg.
     * @param direction  Desired direction.
     * @return GPIO_SUCCESS on success.
     */
    [[nodiscard]] hf_gpio_err_t SetDirection(std::string_view name, hf_gpio_direction_t direction) noexcept;

    /**
     * @brief Configure internal pull-up / pull-down resistors.
     * @param name       Pin name from pincfg.
     * @param pull_mode  Desired pull mode.
     * @return GPIO_SUCCESS on success.
     */
    [[nodiscard]] hf_gpio_err_t SetPullMode(std::string_view name, hf_gpio_pull_mode_t pull_mode) noexcept;

    /**
     * @brief Configure output driver mode (push-pull or open-drain).
     * @param name  Pin name from pincfg.
     * @param mode  Desired output mode.
     * @return GPIO_SUCCESS on success.
     */
    [[nodiscard]] hf_gpio_err_t SetOutputMode(std::string_view name, hf_gpio_output_mode_t mode) noexcept;

    //==========================================================================
    // INTERRUPT SUPPORT — forwarded to underlying BaseGpio driver
    //==========================================================================

    /**
     * @brief Configure interrupt on a named GPIO pin.
     * @param name      Pin name from pincfg
     * @param trigger   Interrupt trigger edge/level
     * @param callback  Function called on interrupt (keep minimal — ISR context)
     * @param user_data Optional user data passed to callback
     * @return GPIO error code
     */
    [[nodiscard]] hf_gpio_err_t ConfigureInterrupt(std::string_view name,
                                                   hf_gpio_interrupt_trigger_t trigger,
                                                   InterruptCallback callback,
                                                   void* user_data = nullptr) noexcept;
    /**
     * @brief Enable a previously configured interrupt on a named pin.
     * @param name  Pin name from pincfg.
     * @return GPIO_SUCCESS on success.
     */
    [[nodiscard]] hf_gpio_err_t EnableInterrupt(std::string_view name) noexcept;

    /**
     * @brief Disable the interrupt on a named pin.
     * @param name  Pin name from pincfg.
     * @return GPIO_SUCCESS on success.
     */
    [[nodiscard]] hf_gpio_err_t DisableInterrupt(std::string_view name) noexcept;

    /**
     * @brief Check whether a pin's underlying driver supports interrupts.
     * @param name  Pin name from pincfg.
     * @return true if interrupts are supported.
     */
    [[nodiscard]] bool SupportsInterrupts(std::string_view name) const noexcept;

    //==========================================================================
    // DIAGNOSTICS
    //==========================================================================

    /**
     * @brief Populate a diagnostics snapshot.
     * @param[out] diagnostics  Struct filled with current counters and state.
     * @return GPIO_SUCCESS on success.
     */
    [[nodiscard]] hf_gpio_err_t GetSystemDiagnostics(GpioSystemDiagnostics& diagnostics) const noexcept;

    /** @brief Log current GPIO state and statistics to the console. */
    void DumpStatistics() const noexcept;

private:
    GpioManager() noexcept = default;
    ~GpioManager() = default;

    bool Initialize() noexcept;

    /** @brief Find entry index by string name. Returns kInvalidIndex if not found. */
    [[nodiscard]] size_t FindByName(std::string_view name) const noexcept;

    /** @brief Maximum registerable GPIO entries (all GPIO + USER pins from pincfg). */
    static constexpr size_t kMaxEntries = static_cast<size_t>(HfFunctionalGpioPin::HF_FUNCTIONAL_GPIO_COUNT);
    static constexpr size_t kInvalidIndex = SIZE_MAX;

    std::array<GpioEntry, kMaxEntries> entries_{};
    size_t registered_count_{0};
    uint64_t init_time_ms_{0};  ///< Timestamp (ms) when Initialize() completed

    uint32_t total_ops_{0};
    uint32_t successful_ops_{0};
    uint32_t failed_ops_{0};
    hf_gpio_err_t last_error_{hf_gpio_err_t::GPIO_SUCCESS};

    std::atomic<bool> initialized_{false};
    mutable RtosMutex mutex_;
};

//==============================================================================
// CONVENIENCE
//==============================================================================

/**
 * @brief Convenience accessor — equivalent to GpioManager::GetInstance().
 * @return Reference to the singleton GpioManager.
 */
[[nodiscard]] inline GpioManager& GetGpioManager() noexcept {
    return GpioManager::GetInstance();
}

#endif // FLUX_GPIO_MANAGER_H_
