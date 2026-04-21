/**
 * @file TemperatureManager.cpp
 * @brief Implementation of the Flux V1 temperature manager.
 *
 * @author HardFOC Team
 * @date 2026
 * @version 1.0
 */

#include "TemperatureManager.h"
#include "AdcManager.h"
#include "core/hf-core-drivers/internal/hf-pincfg/src/hf_functional_pin_config_flux_v1.hpp"
#include "core/hf-core-drivers/internal/hf-internal-interface-wrap/inc/utils/memory_utils.h"
#include "core/hf-core-utils/hf-utils-rtos-wrap/include/OsUtility.h"

#include <algorithm>
#include <cmath>

static constexpr const char* TAG = "FluxTemp";

//==============================================================================
// LOG HELPERS — surface which physical ADC chipset/instance an ADC channel
// lives on so the registration log lines tell the operator more than just
// "ADC ch=N".
//==============================================================================

namespace {

constexpr const char* AdcChipTypeName(uint8_t chip_type) noexcept {
    switch (static_cast<HfAdcChipType>(chip_type)) {
        case HfAdcChipType::ESP32_INTERNAL:   return "ESP32-S3 internal SAR";
        case HfAdcChipType::ADS7952_EXTERNAL: return "ADS7952";
        default:                              return "UNKNOWN";
    }
}

}  // namespace

//==============================================================================
// SINGLETON
//==============================================================================

TemperatureManager& TemperatureManager::GetInstance() noexcept {
    static TemperatureManager instance;
    return instance;
}

//==============================================================================
// INITIALIZATION
//==============================================================================

bool TemperatureManager::EnsureInitialized() noexcept {
    if (initialized_.load(std::memory_order_acquire)) return true;
    MutexLockGuard lock(mutex_);
    if (initialized_.load(std::memory_order_acquire)) return true;
    bool ok = Initialize();
    initialized_.store(ok, std::memory_order_release);
    return ok;
}

bool TemperatureManager::Initialize() noexcept {
    Logger::GetInstance().Info(TAG, "Initializing Flux temperature manager");

    init_time_ms_ = os_get_elapsed_time_msec();

    bool esp_ok = InitializeEsp32Internal();
    bool ntc_ok = InitializeNtcThermistor();
    bool mcp_ok = InitializeMcp9700Thermistor();

    Logger::GetInstance().Info(TAG,
                              "Temperature manager initialized (ESP32=%s, NTC=%s, MCP9700=%s, sensors=%zu)",
                              esp_ok ? "OK" : "FAIL", ntc_ok ? "OK" : "FAIL", mcp_ok ? "OK" : "FAIL",
                              registered_count_);

    return esp_ok || ntc_ok || mcp_ok;
}

bool TemperatureManager::InitializeEsp32Internal() noexcept {
    esp32_temp_ = hf::utils::make_unique_nothrow<EspTemperature>();
    if (!esp32_temp_) {
        Logger::GetInstance().Error(TAG, "Failed to allocate EspTemperature");
        return false;
    }

    if (!esp32_temp_->EnsureInitialized()) {
        Logger::GetInstance().Error(TAG, "EspTemperature initialization failed");
        esp32_temp_.reset();
        return false;
    }

    auto& entry = entries_[registered_count_++];
    entry.name = "ESP32_INTERNAL";
    entry.sensor_type = FluxTempSensorType::ESP32_INTERNAL;
    entry.registered = true;

    Logger::GetInstance().Info(TAG,
        "  Registered ESP32_INTERNAL temperature sensor (on-die SAR diode)");
    return true;
}

bool TemperatureManager::InitializeNtcThermistor() noexcept {
    // The NTC thermistor needs an ADC channel
    auto& adc = AdcManager::GetInstance();
    if (!adc.EnsureInitialized()) {
        Logger::GetInstance().Warn(TAG, "ADC manager not available — skipping NTC");
        return false;
    }

    BaseAdc* adc_driver = adc.Get(HfFunctionalAdcChannel::ESP32_NTC_THERMISTOR);
    if (!adc_driver) {
        Logger::GetInstance().Warn(TAG, "NTC ADC channel not available");
        return false;
    }

    // Get the hardware channel from the pincfg
    const auto* mapping = GetAdcMapping(HfFunctionalAdcChannel::ESP32_NTC_THERMISTOR);
    if (!mapping) {
        Logger::GetInstance().Error(TAG, "NTC ADC mapping not found");
        return false;
    }

    // NTC handler config: 10k NTC, 10k pullup, 3.3V, B=3950
    ntc_temp_handler_config_t ntc_cfg = NTC_TEMP_HANDLER_CONFIG_DEFAULT();
    ntc_cfg.adc_channel                       = mapping->physical_channel;
    ntc_cfg.voltage_divider_series_resistance  = 10000.0f;
    ntc_cfg.beta_value                         = 3950.0f;
    ntc_cfg.reference_voltage                  = 3.3f;

    ntc_handler_ = hf::utils::make_unique_nothrow<NtcTemperatureHandler>(adc_driver, ntc_cfg);
    if (!ntc_handler_) {
        Logger::GetInstance().Error(TAG, "Failed to allocate NTC handler");
        return false;
    }

    if (!ntc_handler_->EnsureInitialized()) {
        Logger::GetInstance().Error(TAG, "NTC handler initialization failed");
        ntc_handler_.reset();
        return false;
    }

    auto& entry = entries_[registered_count_++];
    entry.name = "NTC_THERMISTOR";
    entry.sensor_type = FluxTempSensorType::NTC_THERMISTOR;
    entry.registered = true;

    Logger::GetInstance().Info(TAG,
        "  Registered NTC_THERMISTOR on %s (chip_unit=%u adc_unit=%u ch=%u)",
        AdcChipTypeName(mapping->chip_type),
        static_cast<unsigned>(mapping->chip_unit),
        static_cast<unsigned>(mapping->adc_unit),
        static_cast<unsigned>(mapping->physical_channel));
    return true;
}

bool TemperatureManager::InitializeMcp9700Thermistor() noexcept {
#if !defined(HARDFOC_MCP9700_SUPPORT)
    return false;
#else
    auto& adc = AdcManager::GetInstance();
    if (!adc.EnsureInitialized()) {
        Logger::GetInstance().Warn(TAG, "ADC manager not available — skipping MCP9700");
        return false;
    }

    BaseAdc* adc_driver = adc.Get(HfFunctionalAdcChannel::ADS7952_0_CH8);
    if (!adc_driver) {
        Logger::GetInstance().Warn(TAG, "MCP9700 ADC channel (ADS7952 CH8) not available");
        return false;
    }

    const auto* mapping = GetAdcMapping(HfFunctionalAdcChannel::ADS7952_0_CH8);
    if (!mapping) {
        Logger::GetInstance().Error(TAG, "MCP9700 ADC mapping not found");
        return false;
    }

    mcp9700_handler_ = hf::utils::make_unique_nothrow<Mcp9700TemperatureHandler>(
        adc_driver, mapping->physical_channel, "MCP9700_THERMISTOR");
    if (!mcp9700_handler_) {
        Logger::GetInstance().Error(TAG, "Failed to allocate MCP9700 handler");
        return false;
    }

    if (!mcp9700_handler_->EnsureInitialized()) {
        Logger::GetInstance().Error(TAG, "MCP9700 handler initialization failed");
        mcp9700_handler_.reset();
        return false;
    }

    auto& entry = entries_[registered_count_++];
    entry.name = "MCP9700_THERMISTOR";
    entry.sensor_type = FluxTempSensorType::MCP9700_THERMISTOR;
    entry.registered = true;

    Logger::GetInstance().Info(TAG,
        "  Registered MCP9700_THERMISTOR on %s (chip_unit=%u adc_unit=%u ch=%u)",
        AdcChipTypeName(mapping->chip_type),
        static_cast<unsigned>(mapping->chip_unit),
        static_cast<unsigned>(mapping->adc_unit),
        static_cast<unsigned>(mapping->physical_channel));
    return true;
#endif
}

//==============================================================================
// LOOKUP
//==============================================================================

size_t TemperatureManager::FindByName(std::string_view name) const noexcept {
    for (size_t i = 0; i < registered_count_; ++i) {
        if (entries_[i].registered && entries_[i].name == name) return i;
    }
    return kInvalidIndex;
}

const TempEntry* TemperatureManager::GetSensorEntry(std::string_view name) const noexcept {
    size_t idx = FindByName(name);
    if (idx == kInvalidIndex) return nullptr;
    return &entries_[idx];
}

//==============================================================================
// TEMPERATURE READING
//==============================================================================

TemperatureError TemperatureManager::ReadTemperatureCelsius(std::string_view name, float& temperature_c) noexcept {
    if (!initialized_.load(std::memory_order_acquire)) {
        last_error_.store(TemperatureError::NOT_INITIALIZED, std::memory_order_release);
        return TemperatureError::NOT_INITIALIZED;
    }

    size_t idx = FindByName(name);
    if (idx == kInvalidIndex) {
        last_error_.store(TemperatureError::SENSOR_NOT_FOUND, std::memory_order_release);
        return TemperatureError::SENSOR_NOT_FOUND;
    }

    MutexLockGuard lock(mutex_);
    auto& entry = entries_[idx];
    BaseTemperature* sensor = nullptr;

    switch (entry.sensor_type) {
        case FluxTempSensorType::ESP32_INTERNAL: sensor = esp32_temp_.get(); break;
        case FluxTempSensorType::NTC_THERMISTOR: sensor = ntc_handler_.get(); break;
        case FluxTempSensorType::MCP9700_THERMISTOR: sensor = mcp9700_handler_.get(); break;
        default: {
            last_error_.store(TemperatureError::SENSOR_NOT_FOUND, std::memory_order_release);
            return TemperatureError::SENSOR_NOT_FOUND;
        }
    }
    if (!sensor) {
        last_error_.store(TemperatureError::SENSOR_NOT_AVAILABLE, std::memory_order_release);
        return TemperatureError::SENSOR_NOT_AVAILABLE;
    }

    hf_temp_err_t err = sensor->ReadTemperatureCelsius(&temperature_c);
    if (err == hf_temp_err_t::TEMP_SUCCESS) {
        entry.last_reading_celsius = temperature_c;
        entry.read_count++;
        return TemperatureError::SUCCESS;
    }

    entry.error_count++;
    last_error_.store(TemperatureError::READ_FAILED, std::memory_order_release);
    return TemperatureError::READ_FAILED;
}

TemperatureError TemperatureManager::ReadTemperatureFahrenheit(std::string_view name, float& temperature_f) noexcept {
    float celsius = 0.0f;
    TemperatureError err = ReadTemperatureCelsius(name, celsius);
    if (err != TemperatureError::SUCCESS) return err;
    temperature_f = celsius * 1.8f + 32.0f;
    return TemperatureError::SUCCESS;
}

TemperatureError TemperatureManager::ReadInternalTemperature(float& temperature_c) noexcept {
    return ReadTemperatureCelsius("ESP32_INTERNAL", temperature_c);
}

TemperatureError TemperatureManager::ReadNtcTemperature(float& temperature_c) noexcept {
    return ReadTemperatureCelsius("NTC_THERMISTOR", temperature_c);
}

TemperatureError TemperatureManager::ReadMcp9700Temperature(float& temperature_c) noexcept {
    return ReadTemperatureCelsius("MCP9700_THERMISTOR", temperature_c);
}

TemperatureError TemperatureManager::GetSystemTemperatureStats(float& min_c, float& max_c, float& avg_c) noexcept {
    if (!initialized_.load(std::memory_order_acquire) || registered_count_ == 0) {
        last_error_.store(TemperatureError::NOT_INITIALIZED, std::memory_order_release);
        return TemperatureError::NOT_INITIALIZED;
    }

    float sum = 0.0f;
    min_c = 200.0f;
    max_c = -100.0f;
    uint8_t valid = 0;

    for (size_t i = 0; i < registered_count_; ++i) {
        float temp = 0.0f;
        if (ReadTemperatureCelsius(entries_[i].name, temp) == TemperatureError::SUCCESS) {
            sum += temp;
            if (temp < min_c) min_c = temp;
            if (temp > max_c) max_c = temp;
            ++valid;
        }
    }

    if (valid == 0) {
        last_error_.store(TemperatureError::READ_FAILED, std::memory_order_release);
        return TemperatureError::READ_FAILED;
    }
    avg_c = sum / static_cast<float>(valid);
    return TemperatureError::SUCCESS;
}

//==============================================================================
// DIAGNOSTICS
//==============================================================================

TemperatureError TemperatureManager::GetSystemDiagnostics(TempSystemDiagnostics& diag) const noexcept {
    diag = {};
    diag.total_sensors_registered = static_cast<uint32_t>(registered_count_);
    diag.system_min_temp_celsius = 200.0f;
    diag.system_max_temp_celsius = -100.0f;
    diag.system_avg_temp_celsius = 0.0f;

    float sum = 0.0f;
    uint32_t valid = 0;

    for (size_t i = 0; i < registered_count_; ++i) {
        const auto& e = entries_[i];
        diag.successful_operations += e.read_count;
        diag.failed_operations     += e.error_count;

        uint8_t type_idx = static_cast<uint8_t>(e.sensor_type);
        if (type_idx < std::size(diag.sensors_by_type)) {
            diag.sensors_by_type[type_idx]++;
        }

        if (e.read_count > 0) {
            sum += e.last_reading_celsius;
            if (e.last_reading_celsius < diag.system_min_temp_celsius)
                diag.system_min_temp_celsius = e.last_reading_celsius;
            if (e.last_reading_celsius > diag.system_max_temp_celsius)
                diag.system_max_temp_celsius = e.last_reading_celsius;
            ++valid;
        }
    }

    if (valid > 0) {
        diag.system_avg_temp_celsius = sum / static_cast<float>(valid);
    } else {
        diag.system_min_temp_celsius = 0.0f;
        diag.system_max_temp_celsius = 0.0f;
    }

    diag.total_operations = diag.successful_operations + diag.failed_operations;
    diag.system_uptime_ms = (init_time_ms_ > 0) ? (os_get_elapsed_time_msec() - init_time_ms_) : 0;
    diag.last_error = last_error_.load(std::memory_order_acquire);
    diag.system_healthy = (diag.failed_operations == 0) && (registered_count_ > 0);
    return TemperatureError::SUCCESS;
}

void TemperatureManager::DumpStatistics() const noexcept {
    auto& log = Logger::GetInstance();
    log.Info(TAG, "=== Flux Temperature Manager Statistics ===");
    log.Info(TAG, "  Sensors registered: %zu / %zu", registered_count_, kMaxSensors);

    for (size_t i = 0; i < registered_count_; ++i) {
        const auto& e = entries_[i];
        log.Info(TAG, "  [%.*s] type=%s last=%.2f°C reads=%u errs=%u",
                 static_cast<int>(e.name.size()), e.name.data(),
                 FluxTempSensorTypeToString(e.sensor_type),
                 static_cast<double>(e.last_reading_celsius),
                 e.read_count, e.error_count);
    }
    log.Info(TAG, "=== End Flux Temperature Manager Statistics ===");
}

//==============================================================================
// DEINITIALIZE
//==============================================================================

bool TemperatureManager::Deinitialize() noexcept {
    MutexLockGuard lock(mutex_);
    if (!initialized_.load(std::memory_order_acquire)) return true;

    Logger::GetInstance().Info(TAG, "Deinitializing Flux Temperature manager");

    // Clear all entries
    for (size_t i = 0; i < registered_count_; ++i) {
        entries_[i].registered = false;
        entries_[i].read_count = 0;
        entries_[i].error_count = 0;
    }
    registered_count_ = 0;

    mcp9700_handler_.reset();
    ntc_handler_.reset();
    esp32_temp_.reset();

    initialized_.store(false, std::memory_order_release);
    return true;
}
