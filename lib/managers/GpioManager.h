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
#include "core/hf-core-drivers/internal/hf-internal-interface-wrap/inc/utils/RtosMutex.h"
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
// GPIO MANAGER
//==============================================================================

class GpioManager {
public:
    //==========================================================================
    // SINGLETON AND LIFECYCLE
    //==========================================================================

    static GpioManager& GetInstance() noexcept;

    GpioManager(const GpioManager&) = delete;
    GpioManager& operator=(const GpioManager&) = delete;
    GpioManager(GpioManager&&) = delete;
    GpioManager& operator=(GpioManager&&) = delete;

    [[nodiscard]] bool EnsureInitialized() noexcept;
    [[nodiscard]] bool IsInitialized() const noexcept { return initialized_.load(std::memory_order_acquire); }

    /**
     * @brief Reset GPIO manager to uninitialized state.
     * @return true on success
     */
    bool Deinitialize() noexcept;

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

    [[nodiscard]] hf_gpio_err_t Set(std::string_view name, bool value) noexcept;
    [[nodiscard]] hf_gpio_err_t SetActive(std::string_view name) noexcept;
    [[nodiscard]] hf_gpio_err_t SetInactive(std::string_view name) noexcept;
    [[nodiscard]] hf_gpio_err_t Read(std::string_view name, bool& state) noexcept;
    [[nodiscard]] hf_gpio_err_t Toggle(std::string_view name) noexcept;
    [[nodiscard]] hf_gpio_err_t IsActive(std::string_view name, bool& active) noexcept;

    //==========================================================================
    // PIN CONFIGURATION
    //==========================================================================

    [[nodiscard]] hf_gpio_err_t SetDirection(std::string_view name, hf_gpio_direction_t direction) noexcept;
    [[nodiscard]] hf_gpio_err_t SetPullMode(std::string_view name, hf_gpio_pull_mode_t pull_mode) noexcept;
    [[nodiscard]] hf_gpio_err_t SetOutputMode(std::string_view name, hf_gpio_output_mode_t mode) noexcept;

    //==========================================================================
    // DIAGNOSTICS
    //==========================================================================

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

    std::atomic<bool> initialized_{false};
    mutable RtosMutex mutex_;
};

//==============================================================================
// CONVENIENCE
//==============================================================================

[[nodiscard]] inline GpioManager& GetGpioManager() noexcept {
    return GpioManager::GetInstance();
}

#endif // FLUX_GPIO_MANAGER_H_
