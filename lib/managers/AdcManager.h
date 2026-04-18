/**
 * @file AdcManager.h
 * @brief ADC management for the HardFOC Flux V1 platform.
 *
 * @details The Flux board has only ESP32-C6 internal ADC channels:
 *            - NTC thermistor  (ADC1_CH4, GPIO4)
 *            - VBUS monitor    (ADC1_CH0, GPIO0, 11× voltage divider)
 *
 *          Design improvement over Vortex: fixed-size array of AdcEntry
 *          structs (no heap unordered_map), no TMC9660 ADC dependency.
 *
 * @author HardFOC Team
 * @date 2026
 * @version 1.0
 */

#ifndef FLUX_ADC_MANAGER_H_
#define FLUX_ADC_MANAGER_H_

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string_view>

#include "core/hf-core-drivers/internal/hf-internal-interface-wrap/inc/base/BaseAdc.h"
#include "core/hf-core-drivers/internal/hf-internal-interface-wrap/inc/mcu/esp32/EspAdc.h"
#include "RtosMutex.h"
#include "core/hf-core-drivers/internal/hf-pincfg/src/hf_functional_pin_config_flux_v1.hpp"

// Forward declaration for ADS7952 handler (conditionally compiled)
#ifdef HARDFOC_ADS7952_SUPPORT
class Ads7952Handler;
#endif

//==============================================================================
// ADC ENTRY — struct is stack-allocated; EspAdc driver owned by singleton via
//              unique_ptr (heap-allocated, one per unit)
//==============================================================================

/**
 * @brief A single ADC channel entry in the fixed-size registry.
 */
struct AdcEntry {
    HfFunctionalAdcChannel functional_channel{HfFunctionalAdcChannel::HF_FUNCTIONAL_ADC_COUNT}; ///< Functional enum for this channel
    std::string_view name;              ///< Human-readable name from pincfg
    BaseAdc* driver{nullptr};           ///< Pointer to the ADC driver (owned by the EspAdc array)
    uint8_t adc_unit{0};                ///< ADC unit (0 = ADC1, 1 = ADC2)
    uint8_t hw_channel{0};              ///< Hardware channel within the unit
    float voltage_divider{1.0f};        ///< Divider ratio for scaled readings
    bool registered{false};             ///< Whether this entry is active

    // Statistics
    uint32_t access_count{0};           ///< Total successful read operations on this channel
    uint32_t error_count{0};            ///< Total failed read operations on this channel
};

/**
 * @brief ADC system diagnostics.
 */
struct AdcSystemDiagnostics {
    bool system_healthy;            ///< True when all registered channels are operational
    uint8_t total_channels;         ///< Maximum number of ADC channels available
    uint8_t registered_channels;    ///< Number of channels currently registered
    uint32_t total_reads;           ///< Cumulative successful read count across all channels
    uint32_t total_errors;          ///< Cumulative error count across all channels
    hf_adc_err_t last_error;        ///< Most recent error code from any operation
};

//==============================================================================
// ADC MANAGER
//==============================================================================

/**
 * @class AdcManager
 * @brief Singleton manager for all analogue-to-digital channels on Flux V1.
 *
 * @details Provides a uniform API over the internal ESP32 ADC and optional
 *          external ADS7952 ADC(s). Channels are registered from the pincfg
 *          X-MACRO table at initialisation and can be looked up by name
 *          (linear scan) or by functional enum (O(1) index).
 *
 * @note Call EnsureInitialized() once before any reads. All public methods
 *       are thread-safe (guarded by an internal RTOS mutex).
 */
class AdcManager {
public:
    //==========================================================================
    // SINGLETON AND LIFECYCLE
    //==========================================================================

    /** @brief Return the singleton AdcManager instance. */
    static AdcManager& GetInstance() noexcept;

    AdcManager(const AdcManager&) = delete;
    AdcManager& operator=(const AdcManager&) = delete;
    AdcManager(AdcManager&&) = delete;
    AdcManager& operator=(AdcManager&&) = delete;

    /**
     * @brief Initialise the ADC subsystem if not already initialised.
     * @return true on success or if already initialised.
     */
    [[nodiscard]] bool EnsureInitialized() noexcept;

    /** @brief Check whether the ADC subsystem has been initialised.
     *  @return true if initialised and ready for reads.
     */
    [[nodiscard]] bool IsInitialized() const noexcept { return initialized_.load(std::memory_order_acquire); }

    /**
     * @brief Release all ADC resources and reset to uninitialized state.
     * @return true on success
     */
    bool Deinitialize() noexcept;

    //==========================================================================
    // CHANNEL ACCESS
    //==========================================================================

    /**
     * @brief Get the BaseAdc driver for a named channel.
     * @param name Channel name from the pincfg table (linear scan).
     * @return Pointer to the BaseAdc driver, or nullptr if not found.
     */
    [[nodiscard]] BaseAdc* Get(std::string_view name) noexcept;

    /**
     * @brief Get the BaseAdc driver by functional enum — O(1) lookup.
     * @param channel Functional ADC channel enum.
     * @return Pointer to the BaseAdc driver, or nullptr if not found.
     */
    [[nodiscard]] BaseAdc* Get(HfFunctionalAdcChannel channel) noexcept;

    /**
     * @brief Check whether a named ADC channel is registered.
     * @param name Channel name to look up.
     * @return true if the channel exists in the registry.
     */
    [[nodiscard]] bool Contains(std::string_view name) const noexcept;

    /**
     * @brief Get the number of registered ADC channels.
     * @return Channel count.
     */
    [[nodiscard]] size_t Size() const noexcept;

    //==========================================================================
    // READING OPERATIONS
    //==========================================================================

    /**
     * @brief Read voltage from a channel by name (already divided if divider > 1).
     * @param name       Channel name (linear string search — use enum overload in tight loops)
     * @param voltage    Output: voltage at the input, if divider == 1, or bus voltage
     * @param samples    Number of samples to average (default 1)
     * @return ADC error code
     */
    [[nodiscard]] hf_adc_err_t ReadVoltage(std::string_view name, float& voltage,
                                           uint8_t samples = 1) noexcept;

    /**
     * @brief Read voltage from a channel by enum — O(1) lookup, preferred for real-time paths.
     * @param channel    Functional ADC channel enum
     * @param voltage    Output: voltage at the input, if divider == 1, or bus voltage
     * @param samples    Number of samples to average (default 1)
     * @return ADC error code
     */
    [[nodiscard]] hf_adc_err_t ReadVoltage(HfFunctionalAdcChannel channel, float& voltage,
                                           uint8_t samples = 1) noexcept;

    /**
     * @brief Read raw ADC count from a channel by name.
     * @param name       Channel name (linear string search — use enum overload in tight loops)
     * @param raw_count  Output: raw ADC count
     * @param samples    Number of samples to average (default 1)
     * @return ADC error code
     */
    [[nodiscard]] hf_adc_err_t ReadRaw(std::string_view name, uint32_t& raw_count,
                                       uint8_t samples = 1) noexcept;

    /**
     * @brief Read raw ADC count from a channel by enum — O(1) lookup, preferred for real-time paths.
     * @param channel    Functional ADC channel enum
     * @param raw_count  Output: raw ADC count
     * @param samples    Number of samples to average (default 1)
     * @return ADC error code
     */
    [[nodiscard]] hf_adc_err_t ReadRaw(HfFunctionalAdcChannel channel, uint32_t& raw_count,
                                       uint8_t samples = 1) noexcept;

    //==========================================================================
    // DIAGNOSTICS
    //==========================================================================

    /**
     * @brief Get the most recent error code.
     * @return Last hf_adc_err_t set by any API call
     */
    [[nodiscard]] hf_adc_err_t GetLastError() const noexcept { return last_error_.load(std::memory_order_acquire); }

    /**
     * @brief Populate an AdcSystemDiagnostics snapshot.
     * @param[out] diag Diagnostics structure to fill.
     * @return ADC_SUCCESS on success.
     */
    [[nodiscard]] hf_adc_err_t GetSystemDiagnostics(AdcSystemDiagnostics& diag) const noexcept;

    /** @brief Log ADC channel statistics and health info to the console. */
    void DumpStatistics() const noexcept;

private:
    AdcManager() noexcept = default;
    ~AdcManager() = default;

    bool Initialize() noexcept;

    /** @brief Find entry index by string name. Returns kInvalidIndex if not found. O(N) linear scan. */
    [[nodiscard]] size_t FindByName(std::string_view name) const noexcept;

    /** @brief Find entry index by functional enum. Returns kInvalidIndex if not found. O(1) direct index. */
    [[nodiscard]] size_t FindByEnum(HfFunctionalAdcChannel channel) const noexcept;

    /** @brief Read from an entry by index — shared implementation for both name and enum overloads. */
    [[nodiscard]] hf_adc_err_t ReadVoltageByIndex(size_t idx, float& voltage, uint8_t samples) noexcept;
    [[nodiscard]] hf_adc_err_t ReadRawByIndex(size_t idx, uint32_t& raw_count, uint8_t samples) noexcept;

    //==========================================================================
    // STORAGE — entries are fixed-size in static storage; EspAdc is
    //           heap-allocated via unique_ptr (singleton lifetime)
    //==========================================================================

    static constexpr size_t kMaxChannels =
        static_cast<size_t>(HfFunctionalAdcChannel::HF_FUNCTIONAL_ADC_COUNT);
    static constexpr size_t kInvalidIndex = SIZE_MAX;

    std::array<AdcEntry, kMaxChannels> entries_{};
    std::array<size_t, kMaxChannels> enum_to_index_{}; ///< O(1) enum → entries index lookup
    size_t registered_count_{0};

    // EspAdc instance — only ADC1 used on ESP32-C6
    // Heap-allocated once via unique_ptr; singleton lives forever.
    std::unique_ptr<EspAdc> esp_adc_;

    // ADS7952 external ADC handler(s) — conditionally compiled
    // Supports array of devices on same SPI bus with different CS pins.
    // Currently 1 device on board, designed for expansion.
#ifdef HARDFOC_ADS7952_SUPPORT
    static constexpr size_t kMaxAds7952Devices = 4;
    std::array<std::unique_ptr<Ads7952Handler>, kMaxAds7952Devices> ads7952_handlers_{};
    size_t ads7952_device_count_{0};
#endif

    std::atomic<bool> initialized_{false};
    std::atomic<hf_adc_err_t> last_error_{hf_adc_err_t::ADC_SUCCESS};
    mutable RtosMutex mutex_;
};

//==============================================================================
// CONVENIENCE
//==============================================================================

/**
 * @brief Convenience accessor — equivalent to AdcManager::GetInstance().
 * @return Reference to the singleton AdcManager.
 */
[[nodiscard]] inline AdcManager& GetAdcManager() noexcept {
    return AdcManager::GetInstance();
}

#endif // FLUX_ADC_MANAGER_H_
