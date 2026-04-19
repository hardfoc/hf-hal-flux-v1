/**
 * @file AdcManager.cpp
 * @brief Implementation of the Flux V1 ADC manager.
 *
 * @author HardFOC Team
 * @date 2026
 * @version 1.0
 */

#include "AdcManager.h"
#include "handlers/logger/Logger.h"
#include "core/hf-core-drivers/internal/hf-internal-interface-wrap/inc/utils/memory_utils.h"
#include "core/hf-core-utils/hf-utils-rtos-wrap/include/OsUtility.h"

#ifdef HARDFOC_ADS7952_SUPPORT
#include "Ads7952Handler.h"
#include "CommChannelsManager.h"
#endif

static constexpr const char* TAG = "FluxAdc";

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
    initialized_.store(ok, std::memory_order_release);
    return ok;
}

bool AdcManager::Initialize() noexcept {
    Logger::GetInstance().Info(TAG, "Initializing Flux ADC manager");

    init_time_ms_ = os_get_elapsed_time_msec();

    // Initialize enum-to-index lookup table to invalid
    enum_to_index_.fill(kInvalidIndex);

    // Create one EspAdc for ADC1 (ESP32 has ADC1 and ADC2)
    hf_adc_unit_config_t adc_cfg{};
    adc_cfg.unit_id = 0;  // ADC1

    // Pre-configure channels before Initialize(), matching EspAdc validation rules:
    // channel_configs[index].channel_id must equal index and at least one channel enabled.
    for (size_t i = 0; i < HF_ADC_MAPPING_SIZE; ++i) {
        const auto& mapping = HF_ADC_MAPPING[i];
        if (static_cast<HfAdcChipType>(mapping.chip_type) != HfAdcChipType::ESP32_INTERNAL) {
            continue;
        }

        if (mapping.adc_unit != adc_cfg.unit_id) {
            continue;
        }

        const uint8_t hw_channel = mapping.physical_channel;
        if (hw_channel >= HF_ESP32_ADC_MAX_CHANNELS) {
            Logger::GetInstance().Warn(TAG,
                "Skipping internal ADC mapping with invalid channel %u (max=%u)",
                static_cast<unsigned>(hw_channel),
                static_cast<unsigned>(HF_ESP32_ADC_MAX_CHANNELS - 1));
            continue;
        }

        adc_cfg.channel_configs[hw_channel].channel_id = hw_channel;
        adc_cfg.channel_configs[hw_channel].attenuation = hf_adc_atten_t::ATTEN_DB_12;
        adc_cfg.channel_configs[hw_channel].bitwidth = hf_adc_bitwidth_t::WIDTH_12BIT;
        adc_cfg.channel_configs[hw_channel].enabled = true;
    }

    esp_adc_ = hf::utils::make_unique_nothrow<EspAdc>(adc_cfg);
    
    bool internal_adc_ok = false;
    if (esp_adc_ && esp_adc_->EnsureInitialized()) {
        internal_adc_ok = true;
        
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
            entry.driver = esp_adc_.get();   // All channels go through ADC1
            entry.registered = true;
            enum_to_index_[static_cast<size_t>(fc)] = registered_count_;
            ++registered_count_;

            // Configure the channel on the EspAdc driver
            esp_adc_->ConfigureChannel(mapping.physical_channel, hf_adc_atten_t::ATTEN_DB_12);

            Logger::GetInstance().Info(TAG, "  Registered ADC: %.*s (unit=%u ch=%u div=%.1f)",
                                      static_cast<int>(entry.name.size()), entry.name.data(),
                                      entry.adc_unit, entry.hw_channel,
                                      static_cast<double>(entry.voltage_divider));
        }
    } else {
        Logger::GetInstance().Warn(TAG, "Internal ESP32 ADC init failed — skipping internal channels");
        esp_adc_.reset();
    }

    //==========================================================================
    // ADS7952 EXTERNAL ADC — conditionally compiled
    //==========================================================================
#ifdef HARDFOC_ADS7952_SUPPORT
    // Iterate pincfg ADC mapping for ADS7952_EXTERNAL channels
    // Group by chip_unit (device index) and create one handler per device
    for (size_t i = 0; i < HF_ADC_MAPPING_SIZE; ++i) {
        const auto& mapping = HF_ADC_MAPPING[i];
        if (static_cast<HfAdcChipType>(mapping.chip_type) != HfAdcChipType::ADS7952_EXTERNAL) {
            continue;
        }

        const uint8_t dev_idx = mapping.chip_unit;

        // Create handler for this device if not already created
        if (dev_idx < kMaxAds7952Devices && !ads7952_handlers_[dev_idx]) {
            // Map device index to SPI device ID
            auto spi_dev_id = static_cast<SpiDeviceId>(
                static_cast<uint8_t>(SpiDeviceId::ADS7952_ADC_0) + dev_idx);

            auto& comms = CommChannelsManager::GetInstance();
            BaseSpi* spi = comms.GetSpiDevice(spi_dev_id);
            if (spi) {
                Ads7952HandlerConfig cfg = GetDefaultAds7952Config();
                cfg.device_index = dev_idx;

                ads7952_handlers_[dev_idx] = hf::utils::make_unique_nothrow<Ads7952Handler>(*spi, cfg);
                if (ads7952_handlers_[dev_idx]) {
                    if (ads7952_handlers_[dev_idx]->EnsureInitialized()) {
                        ++ads7952_device_count_;
                        Logger::GetInstance().Info(TAG, "  ADS7952 Dev%u handler initialized", dev_idx);
                    } else {
                        Logger::GetInstance().Error(TAG, "  ADS7952 Dev%u initialization failed", dev_idx);
                        ads7952_handlers_[dev_idx].reset();
                    }
                }
            } else {
                Logger::GetInstance().Warn(TAG, "  ADS7952 Dev%u: SPI device not available", dev_idx);
            }
        }

        // Register this ADS7952 channel in the entries table
        if (dev_idx < kMaxAds7952Devices && ads7952_handlers_[dev_idx] &&
            registered_count_ < kMaxChannels) {
            const auto fc = static_cast<HfFunctionalAdcChannel>(mapping.functional_channel);
            auto& entry = entries_[registered_count_];
            entry.functional_channel = fc;
            entry.name = to_string(fc);
            entry.adc_unit = mapping.adc_unit;
            entry.hw_channel = mapping.physical_channel;
            entry.voltage_divider = mapping.voltage_divider;
            entry.driver = ads7952_handlers_[dev_idx].get();  // BaseAdc* from Ads7952Handler
            entry.registered = true;
            enum_to_index_[static_cast<size_t>(fc)] = registered_count_;
            ++registered_count_;

            Logger::GetInstance().Info(TAG, "  Registered ADC: %.*s (ADS7952 Dev%u ch=%u div=%.1f)",
                                      static_cast<int>(entry.name.size()), entry.name.data(),
                                      dev_idx, entry.hw_channel,
                                      static_cast<double>(entry.voltage_divider));
        }
    }
#endif // HARDFOC_ADS7952_SUPPORT

    Logger::GetInstance().Info(TAG, "ADC manager initialized (%zu channels)", registered_count_);
    return registered_count_ > 0;
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
    }
    return err;
}

hf_adc_err_t AdcManager::ReadVoltage(std::string_view name, float& voltage, uint8_t samples, uint16_t timeBetweenSamples) noexcept {
    if (!initialized_.load(std::memory_order_acquire))
        return hf_adc_err_t::ADC_ERR_NOT_INITIALIZED;

    size_t idx = FindByName(name);
    if (idx == kInvalidIndex)
        return hf_adc_err_t::ADC_ERR_INVALID_CHANNEL;

    return ReadVoltageByIndex(idx, voltage, samples, timeBetweenSamples);
}

hf_adc_err_t AdcManager::ReadVoltage(HfFunctionalAdcChannel channel, float& voltage, uint8_t samples, uint16_t timeBetweenSamples) noexcept {
    if (!initialized_.load(std::memory_order_acquire))
        return hf_adc_err_t::ADC_ERR_NOT_INITIALIZED;

    size_t idx = FindByEnum(channel);
    if (idx == kInvalidIndex)
        return hf_adc_err_t::ADC_ERR_INVALID_CHANNEL;

    return ReadVoltageByIndex(idx, voltage, samples, timeBetweenSamples);
}

hf_adc_err_t AdcManager::ReadRaw(std::string_view name, uint32_t& raw_count, uint8_t samples, uint16_t timeBetweenSamples) noexcept {
    if (!initialized_.load(std::memory_order_acquire))
        return hf_adc_err_t::ADC_ERR_NOT_INITIALIZED;

    size_t idx = FindByName(name);
    if (idx == kInvalidIndex)
        return hf_adc_err_t::ADC_ERR_INVALID_CHANNEL;

    return ReadRawByIndex(idx, raw_count, samples, timeBetweenSamples);
}

hf_adc_err_t AdcManager::ReadRaw(HfFunctionalAdcChannel channel, uint32_t& raw_count, uint8_t samples, uint16_t timeBetweenSamples) noexcept {
    if (!initialized_.load(std::memory_order_acquire))
        return hf_adc_err_t::ADC_ERR_NOT_INITIALIZED;

    size_t idx = FindByEnum(channel);
    if (idx == kInvalidIndex)
        return hf_adc_err_t::ADC_ERR_INVALID_CHANNEL;

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
            uint8_t chip_idx = (entries_[i].driver == esp_adc_.get()) ? 0 : 1;
            if (chip_idx < std::size(diag.channels_by_chip)) {
                diag.channels_by_chip[chip_idx]++;
            }
        }
    }
    diag.total_operations = diag.successful_operations + diag.failed_operations;
    diag.last_error = last_error_.load(std::memory_order_acquire);
    diag.system_uptime_ms = (init_time_ms_ > 0) ? (os_get_elapsed_time_msec() - init_time_ms_) : 0;
    diag.system_healthy = (diag.total_channels_registered > 0) && (diag.failed_operations == 0);
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
    if (!initialized_.load(std::memory_order_acquire)) return true;

    Logger::GetInstance().Info(TAG, "Deinitializing Flux ADC manager");

    // Clear all entries
    for (size_t i = 0; i < registered_count_; ++i) {
        entries_[i].driver = nullptr;
        entries_[i].registered = false;
        entries_[i].access_count = 0;
        entries_[i].error_count = 0;
    }
    registered_count_ = 0;

    // Release the EspAdc instance
    esp_adc_.reset();

    // Release ADS7952 handlers
#ifdef HARDFOC_ADS7952_SUPPORT
    for (auto& handler : ads7952_handlers_) {
        if (handler) {
            handler->Deinitialize();
            handler.reset();
        }
    }
    ads7952_device_count_ = 0;
#endif

    initialized_.store(false, std::memory_order_release);
    return true;
}
