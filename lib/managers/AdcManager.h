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
#include <string_view>

#include "core/hf-core-drivers/internal/hf-internal-interface-wrap/inc/base/BaseAdc.h"
#include "core/hf-core-drivers/internal/hf-internal-interface-wrap/inc/mcu/esp32/EspAdc.h"
#include "core/hf-core-drivers/internal/hf-internal-interface-wrap/inc/utils/RtosMutex.h"
#include "core/hf-core-drivers/internal/hf-pincfg/src/hf_functional_pin_config_flux_v1.hpp"

//==============================================================================
// ADC ENTRY — stack-allocated, owning an EspAdc by pointer (one per unit)
//==============================================================================

/**
 * @brief A single ADC channel entry in the fixed-size registry.
 */
struct AdcEntry {
    HfFunctionalAdcChannel functional_channel{HfFunctionalAdcChannel::HF_FUNCTIONAL_ADC_COUNT};
    std::string_view name;              ///< Human-readable name from pincfg
    BaseAdc* driver{nullptr};           ///< Pointer to the ADC driver (owned by the EspAdc array)
    uint8_t adc_unit{0};                ///< ADC unit (0 = ADC1, 1 = ADC2)
    uint8_t hw_channel{0};              ///< Hardware channel within the unit
    float voltage_divider{1.0f};        ///< Divider ratio for scaled readings
    bool registered{false};             ///< Whether this entry is active

    // Statistics
    uint32_t access_count{0};
    uint32_t error_count{0};
};

/**
 * @brief ADC system diagnostics.
 */
struct FluxAdcDiagnostics {
    bool system_healthy;
    uint8_t total_channels;
    uint8_t registered_channels;
    uint32_t total_reads;
    uint32_t total_errors;
};

//==============================================================================
// ADC MANAGER
//==============================================================================

class AdcManager {
public:
    //==========================================================================
    // SINGLETON AND LIFECYCLE
    //==========================================================================

    static AdcManager& GetInstance() noexcept;

    AdcManager(const AdcManager&) = delete;
    AdcManager& operator=(const AdcManager&) = delete;
    AdcManager(AdcManager&&) = delete;
    AdcManager& operator=(AdcManager&&) = delete;

    [[nodiscard]] bool EnsureInitialized() noexcept;
    [[nodiscard]] bool IsInitialized() const noexcept { return initialized_.load(std::memory_order_acquire); }

    //==========================================================================
    // CHANNEL ACCESS
    //==========================================================================

    /** @brief Get the BaseAdc driver for a named channel. Returns nullptr if not found. */
    [[nodiscard]] BaseAdc* Get(std::string_view name) noexcept;

    /** @brief Get the BaseAdc driver by functional enum. Returns nullptr if not found. */
    [[nodiscard]] BaseAdc* Get(HfFunctionalAdcChannel channel) noexcept;

    /** @brief Check if a named ADC channel exists. */
    [[nodiscard]] bool Contains(std::string_view name) const noexcept;

    /** @brief Number of registered ADC channels. */
    [[nodiscard]] size_t Size() const noexcept;

    //==========================================================================
    // READING OPERATIONS
    //==========================================================================

    /**
     * @brief Read voltage from a channel by name (already divided if divider > 1).
     * @param name       Channel name
     * @param voltage    Output: voltage at the input, if divider == 1, or bus voltage
     * @param samples    Number of samples to average (default 1)
     * @return ADC error code
     */
    [[nodiscard]] hf_adc_err_t ReadVoltage(std::string_view name, float& voltage,
                                           uint8_t samples = 1) noexcept;

    /**
     * @brief Read raw ADC count from a channel by name.
     * @param name       Channel name
     * @param raw_count  Output: raw ADC count
     * @param samples    Number of samples to average (default 1)
     * @return ADC error code
     */
    [[nodiscard]] hf_adc_err_t ReadRaw(std::string_view name, uint32_t& raw_count,
                                       uint8_t samples = 1) noexcept;

    //==========================================================================
    // DIAGNOSTICS
    //==========================================================================

    [[nodiscard]] bool GetSystemDiagnostics(FluxAdcDiagnostics& diag) const noexcept;
    void DumpStatistics() const noexcept;

private:
    AdcManager() noexcept = default;
    ~AdcManager() = default;

    bool Initialize() noexcept;

    /** @brief Find entry index by string name. Returns kInvalidIndex if not found. */
    [[nodiscard]] size_t FindByName(std::string_view name) const noexcept;

    //==========================================================================
    // STORAGE — fixed-size, stack-allocated
    //==========================================================================

    static constexpr size_t kMaxChannels =
        static_cast<size_t>(HfFunctionalAdcChannel::HF_FUNCTIONAL_ADC_COUNT);
    static constexpr size_t kInvalidIndex = SIZE_MAX;

    std::array<AdcEntry, kMaxChannels> entries_{};
    size_t registered_count_{0};

    // EspAdc instances — only ADC1 used on ESP32-C6
    // Allocated once; singleton lives forever.
    void* esp_adc_storage_{nullptr};

    std::atomic<bool> initialized_{false};
    mutable RtosMutex mutex_;
};

//==============================================================================
// CONVENIENCE
//==============================================================================

[[nodiscard]] inline AdcManager& GetAdcManager() noexcept {
    return AdcManager::GetInstance();
}

#endif // FLUX_ADC_MANAGER_H_
