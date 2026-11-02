/**
 * @file AdcManager.cpp
 * @brief Implementation of the Flux V1 ADC manager.
 *
 * @author HardFOC Team
 * @date 2026
 * @version 1.0
 */

#include "AdcManager.h"
#include "bsp_bind/FluxPlatformBind.h"
#include "handlers/logger/Logger.h"
#include "handlers/logger/LogDecorators.h"
#include "core/hf-core-utils/hf-utils-rtos-wrap/include/OsUtility.h"

#include "CommChannelsManager.h"

static constexpr const char* TAG = "FluxAdc";

using flux::hal::ManagerError;

//==============================================================================
// SINGLETON
//==============================================================================

AdcManager& AdcManager::GetInstance() noexcept {
    static AdcManager instance;
    return instance;
}

//==============================================================================
// INITIALIZATION
//==============================================================================

bool AdcManager::EnsureInitialized() noexcept {
    if (initialized_.load(std::memory_order_acquire)) return true;
    MutexLockGuard lock(mutex_);
    if (initialized_.load(std::memory_order_acquire)) return true;
    bool ok = Initialize();
    if (ok) {
        manager_error_.store(ManagerError::None, std::memory_order_release);
    } else {
        last_error_.store(hf_adc_err_t::ADC_ERR_NOT_INITIALIZED, std::memory_order_release);
    }
    initialized_.store(ok, std::memory_order_release);
    return ok;
}

bool AdcManager::IsAds7952Ready(uint8_t device_index) const noexcept {
    return device_index < kMaxAds7952Devices && ads7952_handlers_[device_index].has_value();
}

void AdcManager::ReleaseLocked() noexcept {
    for (size_t i = 0; i < registered_count_; ++i) {
        entries_[i] = AdcEntry{};
    }
    registered_count_ = 0;
    enum_to_index_.fill(kInvalidIndex);
    if (internal_adc_ != nullptr) {
        flux::bsp::DestroyInternalAdc();
        internal_adc_ = nullptr;
    }
    for (auto& handler : ads7952_handlers_) {
        if (handler.has_value()) {
            (void)handler->Deinitialize();
            handler.reset();
        }
    }
    ads7952_device_count_ = 0;
}

bool AdcManager::Initialize() noexcept {
    Logger::GetInstance().Info(TAG, "Initializing Flux ADC manager");

    // A previous failed attempt may have left part of the ADC up; start from nothing.
    ReleaseLocked();

    // Fail-closed bookkeeping: every source the pin map uses must come up.
    bool internal_needed = false;
    bool internal_failed = false;
    std::array<bool, kMaxAds7952Devices> ads_needed{};
    std::array<bool, kMaxAds7952Devices> ads_attempted{};
    ManagerError ads_error = ManagerError::None;

    logger_decor::Section(TAG, "ADC registration table");
    Logger::GetInstance().Info(TAG, "  %-27s %-15s %-8s %-5s %-6s", "Channel", "Source", "Unit", "Ch", "Div");
    Logger::GetInstance().Info(TAG, "  %-27s %-15s %-8s %-5s %-6s", "-------", "------", "----", "--", "---");

    init_time_ms_ = os_get_elapsed_time_msec();

    // Initialize enum-to-index lookup table to invalid
    enum_to_index_.fill(kInvalidIndex);

    // One MCU ADC unit (ADC1). Collect the channels the pin map uses on it; the
    // binding creates the unit with exactly those channels enabled.
    constexpr uint8_t kInternalAdcUnit = 0;  // ADC1
    const uint8_t max_channels = flux::bsp::InternalAdcChannelCount();
    uint32_t channel_mask = 0;

    for (size_t i = 0; i < HF_ADC_MAPPING_SIZE; ++i) {
        const auto& mapping = HF_ADC_MAPPING[i];
        if (static_cast<HfAdcChipType>(mapping.chip_type) != HfAdcChipType::ESP32_INTERNAL) {
            continue;
        }

        if (mapping.adc_unit != kInternalAdcUnit) {
            continue;
        }

        const uint8_t hw_channel = mapping.physical_channel;
        if (hw_channel >= max_channels || hw_channel >= 32U) {
            Logger::GetInstance().Warn(TAG,
                "Skipping internal ADC mapping with invalid channel %u (max=%u)",
                static_cast<unsigned>(hw_channel),
                static_cast<unsigned>(max_channels > 0 ? max_channels - 1 : 0));
            continue;
        }
        channel_mask |= (1UL << hw_channel);
    }

    internal_needed = (channel_mask != 0U);
    internal_adc_ = internal_needed ? flux::bsp::CreateInternalAdc(kInternalAdcUnit, channel_mask) : nullptr;
    internal_failed = internal_needed && (internal_adc_ == nullptr);

    if (internal_adc_ != nullptr) {
        // Iterate the pincfg ADC mapping and register each channel
        for (size_t i = 0; i < HF_ADC_MAPPING_SIZE; ++i) {
            const auto& mapping = HF_ADC_MAPPING[i];
            const auto fc = static_cast<HfFunctionalAdcChannel>(mapping.functional_channel);

            // Only ESP32_INTERNAL channels on this pass
            if (static_cast<HfAdcChipType>(mapping.chip_type) != HfAdcChipType::ESP32_INTERNAL) {
                continue;
            }

            auto& entry = entries_[registered_count_];
            entry.functional_channel = fc;
            entry.name = to_string(fc);
            entry.adc_unit = mapping.adc_unit;
            entry.hw_channel = mapping.physical_channel;
            entry.voltage_divider = mapping.voltage_divider;
            entry.driver = internal_adc_;    // All channels go through ADC1
            entry.registered = true;
            enum_to_index_[static_cast<size_t>(fc)] = registered_count_;
            ++registered_count_;

            Logger::GetInstance().Info(TAG, "  %-27.*s %-15s %-8u %-5u %-6.1f",
                                      static_cast<int>(entry.name.size()), entry.name.data(),
                                      "ESP32_INTERNAL",
                                      entry.adc_unit, entry.hw_channel,
                                      static_cast<double>(entry.voltage_divider));
        }
    } else {
        Logger::GetInstance().Warn(TAG, "Internal MCU ADC init failed — skipping internal channels");
    }

    //==========================================================================
    // ADS7952 EXTERNAL ADC — one handler per device, constructed in place
    //==========================================================================
    // Iterate pincfg ADC mapping for ADS7952_EXTERNAL channels
    // Group by chip_unit (device index) and create one handler per device
    for (size_t i = 0; i < HF_ADC_MAPPING_SIZE; ++i) {
        const auto& mapping = HF_ADC_MAPPING[i];
        if (static_cast<HfAdcChipType>(mapping.chip_type) != HfAdcChipType::ADS7952_EXTERNAL) {
            continue;
        }

        const uint8_t dev_idx = mapping.chip_unit;
        if (dev_idx >= kMaxAds7952Devices) {
            Logger::GetInstance().Error(TAG, "  [ADS7952] Dev%u out of range (max %u)", dev_idx,
                                        static_cast<unsigned>(kMaxAds7952Devices - 1));
            ads_error = ManagerError::BindFailed;
            continue;
        }
        ads_needed[dev_idx] = true;

        // Create the handler for this device once (first channel that names it)
        if (!ads_attempted[dev_idx]) {
            ads_attempted[dev_idx] = true;
            // Map device index to SPI device ID
            auto spi_dev_id = static_cast<SpiDeviceId>(
                static_cast<uint8_t>(SpiDeviceId::ADS7952_ADC_0) + dev_idx);

            auto& comms = CommChannelsManager::GetInstance();
            BaseSpi* spi = comms.GetSpiDevice(spi_dev_id);
            if (spi) {
                Ads7952HandlerConfig cfg = GetDefaultAds7952Config();
                cfg.device_index = dev_idx;

                auto& handler = ads7952_handlers_[dev_idx].emplace(*spi, cfg);
                if (handler.EnsureInitialized()) {
                    ++ads7952_device_count_;
                    Logger::GetInstance().Info(TAG, "  [ADS7952] Dev%u handler initialized", dev_idx);
                } else {
                    Logger::GetInstance().Error(TAG, "  [ADS7952] Dev%u initialization failed", dev_idx);
                    ads7952_handlers_[dev_idx].reset();
                    if (ads_error == ManagerError::None) ads_error = ManagerError::HardwareFault;
                }
            } else {
                Logger::GetInstance().Error(TAG, "  [ADS7952] Dev%u: SPI device not available", dev_idx);
                ads_error = ManagerError::BindFailed;
            }
        }

        // Register this ADS7952 channel in the entries table
        if (ads7952_handlers_[dev_idx].has_value() && registered_count_ < kMaxChannels) {
            const auto fc = static_cast<HfFunctionalAdcChannel>(mapping.functional_channel);
            auto& entry = entries_[registered_count_];
            entry.functional_channel = fc;
            entry.name = to_string(fc);
            entry.adc_unit = mapping.adc_unit;
            entry.hw_channel = mapping.physical_channel;
            entry.voltage_divider = mapping.voltage_divider;
            entry.driver = &*ads7952_handlers_[dev_idx];  // BaseAdc* from Ads7952Handler
            entry.registered = true;
            enum_to_index_[static_cast<size_t>(fc)] = registered_count_;
            ++registered_count_;

            char src[24];
            std::snprintf(src, sizeof(src), "ADS7952_DEV%u", static_cast<unsigned>(dev_idx));
            Logger::GetInstance().Info(TAG, "  %-27.*s %-15s %-8u %-5u %-6.1f",
                                      static_cast<int>(entry.name.size()), entry.name.data(),
                                      src,
                                      entry.adc_unit, entry.hw_channel,
                                      static_cast<double>(entry.voltage_divider));
        }
    }

    logger_decor::Rule(TAG);

    // ── Fail closed: the MCU ADC (if the pin map uses it) and every ADS7952 the pin map
    //    names must be up. Otherwise the manager stays uninitialised and Get() returns nullptr.
    ManagerError err = ManagerError::None;
    if (internal_failed) {
        err = ManagerError::BindFailed;
    } else if (ads_error != ManagerError::None) {
        err = ads_error;
    } else if (registered_count_ == 0) {
        err = ManagerError::BindFailed;  // nothing in the pin map at all
    }
    for (size_t d = 0; d < kMaxAds7952Devices && err == ManagerError::None; ++d) {
        if (ads_needed[d] && !ads7952_handlers_[d].has_value()) {
            err = ManagerError::HardwareFault;
        }
    }

    if (err != ManagerError::None) {
        manager_error_.store(err, std::memory_order_release);
        Logger::GetInstance().Error(TAG,
            "ADC manager NOT initialised (fail-closed, %s): internal ADC=%s, ADS7952 dev0=%s",
            flux::hal::ToString(err),
            internal_needed ? (internal_failed ? "FAIL" : "OK") : "unused",
            ads_needed[0] ? (ads7952_handlers_[0].has_value() ? "OK" : "FAIL") : "unused");
        return false;
    }

    Logger::GetInstance().Info(TAG, "ADC manager initialized (%zu channels)", registered_count_);
    return true;
}

//==============================================================================
// LOOKUP
//==============================================================================

size_t AdcManager::FindByName(std::string_view name) const noexcept {
    for (size_t i = 0; i < registered_count_; ++i) {
        if (entries_[i].registered && entries_[i].name == name) return i;
    }
    return kInvalidIndex;
}

size_t AdcManager::FindByEnum(HfFunctionalAdcChannel channel) const noexcept {
    const auto idx = static_cast<size_t>(channel);
    if (idx >= kMaxChannels) return kInvalidIndex;
    return enum_to_index_[idx];  // O(1) — populated during Initialize()
}

BaseAdc* AdcManager::Get(std::string_view name) noexcept {
    if (!initialized_.load(std::memory_order_acquire)) return nullptr;
    size_t idx = FindByName(name);
    if (idx == kInvalidIndex) return nullptr;
    entries_[idx].access_count++;
    return entries_[idx].driver;
}

BaseAdc* AdcManager::Get(HfFunctionalAdcChannel channel) noexcept {
    if (!initialized_.load(std::memory_order_acquire)) return nullptr;
    size_t idx = FindByEnum(channel);
    if (idx == kInvalidIndex) return nullptr;
    entries_[idx].access_count++;
    return entries_[idx].driver;
}

bool AdcManager::Contains(std::string_view name) const noexcept {
    if (!initialized_.load(std::memory_order_acquire)) return false;
    return FindByName(name) != kInvalidIndex;
}

size_t AdcManager::Size() const noexcept {
    return registered_count_;
}

//==============================================================================
// READING OPERATIONS
//==============================================================================

hf_adc_err_t AdcManager::ReadVoltageByIndex(size_t idx, float& voltage, uint8_t samples, uint16_t timeBetweenSamples) noexcept {
    auto& entry = entries_[idx];
    MutexLockGuard lock(mutex_);

    float raw_voltage = 0.0f;
    hf_adc_err_t err = entry.driver->ReadChannelV(entry.hw_channel, raw_voltage, samples, timeBetweenSamples);
    if (err == hf_adc_err_t::ADC_SUCCESS) {
        voltage = raw_voltage * entry.voltage_divider;
        entry.access_count++;
    } else {
        entry.error_count++;
        last_error_.store(err, std::memory_order_release);
    }
    return err;
}

hf_adc_err_t AdcManager::ReadRawByIndex(size_t idx, uint32_t& raw_count, uint8_t samples, uint16_t timeBetweenSamples) noexcept {
    auto& entry = entries_[idx];
    MutexLockGuard lock(mutex_);

    hf_adc_err_t err = entry.driver->ReadChannelCount(entry.hw_channel, raw_count, samples, timeBetweenSamples);
    if (err == hf_adc_err_t::ADC_SUCCESS) {
        entry.access_count++;
    } else {
        entry.error_count++;
        last_error_.store(err, std::memory_order_release);
    }
    return err;
}

hf_adc_err_t AdcManager::ReadVoltage(std::string_view name, float& voltage, uint8_t samples, uint16_t timeBetweenSamples) noexcept {
    if (!initialized_.load(std::memory_order_acquire)) {
        last_error_.store(hf_adc_err_t::ADC_ERR_NOT_INITIALIZED, std::memory_order_release);
        return hf_adc_err_t::ADC_ERR_NOT_INITIALIZED;
    }

    size_t idx = FindByName(name);
    if (idx == kInvalidIndex) {
        last_error_.store(hf_adc_err_t::ADC_ERR_INVALID_CHANNEL, std::memory_order_release);
        return hf_adc_err_t::ADC_ERR_INVALID_CHANNEL;
    }

    return ReadVoltageByIndex(idx, voltage, samples, timeBetweenSamples);
}

hf_adc_err_t AdcManager::ReadVoltage(HfFunctionalAdcChannel channel, float& voltage, uint8_t samples, uint16_t timeBetweenSamples) noexcept {
    if (!initialized_.load(std::memory_order_acquire)) {
        last_error_.store(hf_adc_err_t::ADC_ERR_NOT_INITIALIZED, std::memory_order_release);
        return hf_adc_err_t::ADC_ERR_NOT_INITIALIZED;
    }

    size_t idx = FindByEnum(channel);
    if (idx == kInvalidIndex) {
        last_error_.store(hf_adc_err_t::ADC_ERR_INVALID_CHANNEL, std::memory_order_release);
        return hf_adc_err_t::ADC_ERR_INVALID_CHANNEL;
    }

    return ReadVoltageByIndex(idx, voltage, samples, timeBetweenSamples);
}

hf_adc_err_t AdcManager::ReadRaw(std::string_view name, uint32_t& raw_count, uint8_t samples, uint16_t timeBetweenSamples) noexcept {
    if (!initialized_.load(std::memory_order_acquire)) {
        last_error_.store(hf_adc_err_t::ADC_ERR_NOT_INITIALIZED, std::memory_order_release);
        return hf_adc_err_t::ADC_ERR_NOT_INITIALIZED;
    }

    size_t idx = FindByName(name);
    if (idx == kInvalidIndex) {
        last_error_.store(hf_adc_err_t::ADC_ERR_INVALID_CHANNEL, std::memory_order_release);
        return hf_adc_err_t::ADC_ERR_INVALID_CHANNEL;
    }

    return ReadRawByIndex(idx, raw_count, samples, timeBetweenSamples);
}

hf_adc_err_t AdcManager::ReadRaw(HfFunctionalAdcChannel channel, uint32_t& raw_count, uint8_t samples, uint16_t timeBetweenSamples) noexcept {
    if (!initialized_.load(std::memory_order_acquire)) {
        last_error_.store(hf_adc_err_t::ADC_ERR_NOT_INITIALIZED, std::memory_order_release);
        return hf_adc_err_t::ADC_ERR_NOT_INITIALIZED;
    }

    size_t idx = FindByEnum(channel);
    if (idx == kInvalidIndex) {
        last_error_.store(hf_adc_err_t::ADC_ERR_INVALID_CHANNEL, std::memory_order_release);
        return hf_adc_err_t::ADC_ERR_INVALID_CHANNEL;
    }

    return ReadRawByIndex(idx, raw_count, samples, timeBetweenSamples);
}

//==============================================================================
// DIAGNOSTICS
//==============================================================================

hf_adc_err_t AdcManager::GetSystemDiagnostics(AdcSystemDiagnostics& diag) const noexcept {
    diag = {};
    diag.total_channels_registered = static_cast<uint32_t>(registered_count_);

    for (size_t i = 0; i < registered_count_; ++i) {
        if (entries_[i].registered) {
            diag.successful_operations += entries_[i].access_count;
            diag.failed_operations     += entries_[i].error_count;

            // Chip index: 0 = ESP32 internal, 1 = external (ADS7952)
            uint8_t chip_idx = (entries_[i].driver == internal_adc_) ? 0 : 1;
            if (chip_idx < std::size(diag.channels_by_chip)) {
                diag.channels_by_chip[chip_idx]++;
            }
        }
    }
    diag.total_operations = diag.successful_operations + diag.failed_operations;
    diag.last_error = last_error_.load(std::memory_order_acquire);
    diag.system_uptime_ms = (init_time_ms_ > 0) ? (os_get_elapsed_time_msec() - init_time_ms_) : 0;
    diag.system_healthy = initialized_.load(std::memory_order_acquire) && (diag.failed_operations == 0);
    return hf_adc_err_t::ADC_SUCCESS;
}

void AdcManager::DumpStatistics() const noexcept {
    auto& log = Logger::GetInstance();
    log.Info(TAG, "=== Flux ADC Manager Statistics ===");
    log.Info(TAG, "  Channels registered: %zu / %zu", registered_count_, kMaxChannels);

    for (size_t i = 0; i < registered_count_; ++i) {
        const auto& e = entries_[i];
        log.Info(TAG, "  [%.*s] unit=%u ch=%u div=%.1f reads=%u errs=%u",
                 static_cast<int>(e.name.size()), e.name.data(),
                 e.adc_unit, e.hw_channel,
                 static_cast<double>(e.voltage_divider),
                 e.access_count, e.error_count);
    }
    log.Info(TAG, "=== End Flux ADC Manager Statistics ===");
}

//==============================================================================
// DEINITIALIZE
//==============================================================================

bool AdcManager::Deinitialize() noexcept {
    MutexLockGuard lock(mutex_);
    if (initialized_.load(std::memory_order_acquire)) {
        Logger::GetInstance().Info(TAG, "Deinitializing Flux ADC manager");
    }
    ReleaseLocked();
    initialized_.store(false, std::memory_order_release);
    manager_error_.store(ManagerError::NotInitialized, std::memory_order_release);
    return true;
}
