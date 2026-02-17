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
#include "core/hf-core-drivers/internal/hf-internal-interface-wrap/inc/mcu/esp32/EspAdc.h"

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

    // Create one EspAdc for ADC1 (ESP32-C6 only has ADC1 usable)
    auto* adc1 = new (std::nothrow) EspAdc(0, 3.3f);  // unit 0 = ADC1
    if (!adc1) {
        Logger::GetInstance().Error(TAG, "Failed to allocate EspAdc unit 0");
        return false;
    }
    esp_adc_storage_ = adc1;

    if (!adc1->Initialize()) {
        Logger::GetInstance().Error(TAG, "Failed to initialize EspAdc unit 0");
        return false;
    }

    // Iterate the pincfg ADC mapping and register each channel
    for (size_t i = 0; i < HF_ADC_MAPPING_SIZE; ++i) {
        const auto& mapping = HF_ADC_MAPPING[i];
        const auto fc = static_cast<HfFunctionalAdcChannel>(mapping.functional_channel);

        // Only ESP32_INTERNAL channels on Flux
        if (static_cast<HfAdcChipType>(mapping.chip_type) != HfAdcChipType::ESP32_INTERNAL) {
            continue;
        }

        auto& entry = entries_[registered_count_];
        entry.functional_channel = fc;
        entry.name = to_string(fc);
        entry.adc_unit = mapping.adc_unit;
        entry.hw_channel = mapping.channel;
        entry.voltage_divider = mapping.voltage_divider;
        entry.driver = adc1;   // All channels go through ADC1
        entry.registered = true;
        ++registered_count_;

        // Configure the channel on the EspAdc driver
        adc1->ConfigureChannel(mapping.channel);

        Logger::GetInstance().Info(TAG, "  Registered ADC: %.*s (unit=%u ch=%u div=%.1f)",
                                  static_cast<int>(entry.name.size()), entry.name.data(),
                                  entry.adc_unit, entry.hw_channel,
                                  static_cast<double>(entry.voltage_divider));
    }

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

BaseAdc* AdcManager::Get(std::string_view name) noexcept {
    if (!initialized_.load(std::memory_order_acquire)) return nullptr;
    size_t idx = FindByName(name);
    if (idx == kInvalidIndex) return nullptr;
    entries_[idx].access_count++;
    return entries_[idx].driver;
}

BaseAdc* AdcManager::Get(HfFunctionalAdcChannel channel) noexcept {
    if (!initialized_.load(std::memory_order_acquire)) return nullptr;
    for (size_t i = 0; i < registered_count_; ++i) {
        if (entries_[i].registered && entries_[i].functional_channel == channel) {
            entries_[i].access_count++;
            return entries_[i].driver;
        }
    }
    return nullptr;
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

hf_adc_err_t AdcManager::ReadVoltage(std::string_view name, float& voltage, uint8_t samples) noexcept {
    if (!initialized_.load(std::memory_order_acquire))
        return hf_adc_err_t::ADC_ERR_NOT_INITIALIZED;

    size_t idx = FindByName(name);
    if (idx == kInvalidIndex)
        return hf_adc_err_t::ADC_ERR_INVALID_CHANNEL;

    auto& entry = entries_[idx];
    MutexLockGuard lock(mutex_);

    float raw_voltage = 0.0f;
    hf_adc_err_t err = entry.driver->ReadVoltage(entry.hw_channel, raw_voltage, samples, 0);
    if (err == hf_adc_err_t::ADC_SUCCESS) {
        voltage = raw_voltage * entry.voltage_divider;
        entry.access_count++;
    } else {
        entry.error_count++;
    }
    return err;
}

hf_adc_err_t AdcManager::ReadRaw(std::string_view name, uint32_t& raw_count, uint8_t samples) noexcept {
    if (!initialized_.load(std::memory_order_acquire))
        return hf_adc_err_t::ADC_ERR_NOT_INITIALIZED;

    size_t idx = FindByName(name);
    if (idx == kInvalidIndex)
        return hf_adc_err_t::ADC_ERR_INVALID_CHANNEL;

    auto& entry = entries_[idx];
    MutexLockGuard lock(mutex_);

    hf_adc_err_t err = entry.driver->ReadRaw(entry.hw_channel, raw_count, samples, 0);
    if (err == hf_adc_err_t::ADC_SUCCESS) {
        entry.access_count++;
    } else {
        entry.error_count++;
    }
    return err;
}

//==============================================================================
// DIAGNOSTICS
//==============================================================================

bool AdcManager::GetSystemDiagnostics(FluxAdcDiagnostics& diag) const noexcept {
    diag.total_channels = static_cast<uint8_t>(kMaxChannels);
    diag.registered_channels = static_cast<uint8_t>(registered_count_);
    diag.total_reads = 0;
    diag.total_errors = 0;

    for (size_t i = 0; i < registered_count_; ++i) {
        diag.total_reads  += entries_[i].access_count;
        diag.total_errors += entries_[i].error_count;
    }
    diag.system_healthy = (diag.registered_channels > 0) && (diag.total_errors == 0);
    return true;
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
