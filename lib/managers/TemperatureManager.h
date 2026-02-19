/**
 * @file TemperatureManager.h
 * @brief Temperature sensor management for the HardFOC Flux V1 platform.
 *
 * @details The Flux board has two temperature sources:
 *            1. ESP32-C6 internal temperature sensor (on-chip)
 *            2. NTC thermistor via ADC (GPIO4 / ADC1_CH4)
 *
 *          No TMC9660 temperature sensor (Flux has no motor controller).
 *
 *          Stack-friendly design: fixed-size array of TempEntry structs,
 *          no heap vectors or unordered_maps. Sensor driver instances are
 *          individually heap-allocated via unique_ptr (singleton lifetime).
 *
 * @author HardFOC Team
 * @date 2026
 * @version 1.0
 */

#ifndef FLUX_TEMPERATURE_MANAGER_H_
#define FLUX_TEMPERATURE_MANAGER_H_

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string_view>

#include "core/hf-core-drivers/internal/hf-internal-interface-wrap/inc/base/BaseTemperature.h"
#include "core/hf-core-drivers/internal/hf-internal-interface-wrap/inc/mcu/esp32/EspTemperature.h"
#include "handlers/ntc/NtcTemperatureHandler.h"
#include "handlers/logger/Logger.h"
#include "core/hf-core-drivers/internal/hf-internal-interface-wrap/inc/utils/RtosMutex.h"

//==============================================================================
// TEMPERATURE ERROR CODES
//==============================================================================

/**
 * @brief Temperature manager error codes for consistent error reporting.
 *
 * Manager-layer errors that are sensor-family agnostic. Driver-level errors
 * (hf_temp_err_t) are translated into this common manager error domain.
 */
enum class TemperatureError : uint8_t {
    SUCCESS = 0,
    NOT_INITIALIZED,
    INITIALIZATION_FAILED,
    SENSOR_NOT_FOUND,
    SENSOR_NOT_AVAILABLE,
    READ_FAILED,
    INVALID_PARAMETER,
    NO_SENSORS_AVAILABLE,
    ALLOCATION_FAILED,
    DEPENDENCY_NOT_READY,
    MUTEX_LOCK_FAILED
};

/** @brief Convert TemperatureError to string for debugging. */
constexpr const char* TemperatureErrorToString(TemperatureError error) noexcept {
    switch (error) {
        case TemperatureError::SUCCESS:               return "Success";
        case TemperatureError::NOT_INITIALIZED:       return "Not initialized";
        case TemperatureError::INITIALIZATION_FAILED: return "Initialization failed";
        case TemperatureError::SENSOR_NOT_FOUND:      return "Sensor not found";
        case TemperatureError::SENSOR_NOT_AVAILABLE:  return "Sensor not available";
        case TemperatureError::READ_FAILED:           return "Read failed";
        case TemperatureError::INVALID_PARAMETER:     return "Invalid parameter";
        case TemperatureError::NO_SENSORS_AVAILABLE:  return "No sensors available";
        case TemperatureError::ALLOCATION_FAILED:     return "Allocation failed";
        case TemperatureError::DEPENDENCY_NOT_READY:  return "Dependency not ready";
        case TemperatureError::MUTEX_LOCK_FAILED:     return "Mutex lock failed";
        default:                                      return "Unknown error";
    }
}

//==============================================================================
// SENSOR TYPE ENUM
//==============================================================================

enum class FluxTempSensorType : uint8_t {
    ESP32_INTERNAL = 0,
    NTC_THERMISTOR = 1,
    SENSOR_COUNT
};

constexpr const char* FluxTempSensorTypeToString(FluxTempSensorType type) noexcept {
    switch (type) {
        case FluxTempSensorType::ESP32_INTERNAL: return "ESP32 Internal";
        case FluxTempSensorType::NTC_THERMISTOR: return "NTC Thermistor";
        default: return "Unknown";
    }
}

//==============================================================================
// TEMPERATURE ENTRY — stack-allocated metadata (drivers owned via unique_ptr)
//==============================================================================

struct TempEntry {
    std::string_view name;                   ///< Human-readable name
    FluxTempSensorType sensor_type{FluxTempSensorType::SENSOR_COUNT};
    bool registered{false};
    float last_reading_celsius{0.0f};

    // Statistics
    uint32_t read_count{0};
    uint32_t error_count{0};
};

//==============================================================================
// TEMPERATURE DIAGNOSTICS
//==============================================================================

struct FluxTempDiagnostics {
    bool system_healthy;
    uint8_t total_sensors;
    uint8_t registered_sensors;
    uint32_t total_reads;
    uint32_t total_errors;
    float esp32_temp_celsius;
    float ntc_temp_celsius;
};

//==============================================================================
// TEMPERATURE MANAGER
//==============================================================================

class TemperatureManager {
public:
    //==========================================================================
    // SINGLETON AND LIFECYCLE
    //==========================================================================

    static TemperatureManager& GetInstance() noexcept;

    TemperatureManager(const TemperatureManager&) = delete;
    TemperatureManager& operator=(const TemperatureManager&) = delete;
    TemperatureManager(TemperatureManager&&) = delete;
    TemperatureManager& operator=(TemperatureManager&&) = delete;

    [[nodiscard]] bool EnsureInitialized() noexcept;
    [[nodiscard]] bool IsInitialized() const noexcept { return initialized_.load(std::memory_order_acquire); }

    /**
     * @brief Release all temperature sensor resources and reset to uninitialized state.
     * @return true on success
     */
    bool Deinitialize() noexcept;

    //==========================================================================
    // TEMPERATURE READING
    //==========================================================================

    /**
     * @brief Read temperature in Celsius from a named sensor.
     * @param name           "ESP32_INTERNAL" or "NTC_THERMISTOR"
     * @param temperature_c  Output temperature in °C
     * @return true on success
     */
    [[nodiscard]] bool ReadTemperatureCelsius(std::string_view name, float& temperature_c) noexcept;

    /**
     * @brief Read temperature in Fahrenheit.
     */
    [[nodiscard]] bool ReadTemperatureFahrenheit(std::string_view name, float& temperature_f) noexcept;

    /**
     * @brief Convenience: read ESP32 internal temperature.
     */
    [[nodiscard]] bool ReadInternalTemperature(float& temperature_c) noexcept;

    /**
     * @brief Convenience: read NTC thermistor temperature.
     */
    [[nodiscard]] bool ReadNtcTemperature(float& temperature_c) noexcept;

    /**
     * @brief Read all sensors and return min/max/avg.
     */
    [[nodiscard]] bool GetSystemTemperatureStats(float& min_c, float& max_c, float& avg_c) noexcept;

    //==========================================================================
    // SENSOR INFO
    //==========================================================================

    [[nodiscard]] size_t GetSensorCount() const noexcept { return registered_count_; }
    [[nodiscard]] const TempEntry* GetSensorEntry(std::string_view name) const noexcept;

    //==========================================================================
    // DIAGNOSTICS
    //==========================================================================

    [[nodiscard]] bool GetSystemDiagnostics(FluxTempDiagnostics& diag) noexcept;
    void DumpStatistics() const noexcept;

private:
    TemperatureManager() noexcept = default;
    ~TemperatureManager() = default;

    bool Initialize() noexcept;
    bool InitializeEsp32Internal() noexcept;
    bool InitializeNtcThermistor() noexcept;

    [[nodiscard]] size_t FindByName(std::string_view name) const noexcept;

    //==========================================================================
    // STORAGE
    //==========================================================================

    static constexpr size_t kMaxSensors = static_cast<size_t>(FluxTempSensorType::SENSOR_COUNT);
    static constexpr size_t kInvalidIndex = SIZE_MAX;

    std::array<TempEntry, kMaxSensors> entries_{};
    size_t registered_count_{0};

    // Sensor driver instances — heap-allocated via unique_ptr (singleton lifetime)
    std::unique_ptr<EspTemperature> esp32_temp_;
    std::unique_ptr<NtcTemperatureHandler> ntc_handler_;

    std::atomic<bool> initialized_{false};
    mutable RtosMutex mutex_;
};

//==============================================================================
// CONVENIENCE
//==============================================================================

[[nodiscard]] inline TemperatureManager& GetTemperatureManager() noexcept {
    return TemperatureManager::GetInstance();
}

#endif // FLUX_TEMPERATURE_MANAGER_H_
