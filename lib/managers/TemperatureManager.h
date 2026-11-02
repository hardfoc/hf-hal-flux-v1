/**
 * @file TemperatureManager.h
 * @brief Temperature sensor management for the HardFOC Flux V1 platform.
 *
 * @details The Flux board temperature sources include:
 *            1. ESP32 internal temperature sensor (on-chip)
 *            2. NTC thermistor on the internal ADC
 *            3. MCP9700 on ADS7952 channel 8 (when the external ADC is present)
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
#include "handlers/mcp9700/Mcp9700TemperatureHandler.h"
#include "handlers/ntc/NtcTemperatureHandler.h"
#include "handlers/logger/Logger.h"
#include "RtosMutex.h"

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
    SUCCESS = 0,              ///< Operation completed successfully
    NOT_INITIALIZED,          ///< Manager not yet initialised
    INITIALIZATION_FAILED,    ///< Sensor initialisation failed
    SENSOR_NOT_FOUND,         ///< Named sensor does not exist in registry
    SENSOR_NOT_AVAILABLE,     ///< Sensor exists but is unavailable
    READ_FAILED,              ///< Driver-level read returned an error
    INVALID_PARAMETER,        ///< Null pointer or out-of-range argument
    NO_SENSORS_AVAILABLE,     ///< No sensors were registered at init
    ALLOCATION_FAILED,        ///< Heap allocation for a sensor driver failed
    DEPENDENCY_NOT_READY,     ///< Required dependency not initialised
    MUTEX_LOCK_FAILED         ///< RTOS mutex acquire timed out
};

/**
 * @brief Convert TemperatureError to string for debugging.
 * @param error Error code to convert.
 * @return Null-terminated string representation.
 */
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

/**
 * @brief Identifies the physical temperature sensor type on the Flux board.
 */
enum class FluxTempSensorType : uint8_t {
    ESP32_INTERNAL = 0, ///< ESP32 on-chip temperature sensor
    NTC_THERMISTOR = 1, ///< External NTC thermistor via ADC
    MCP9700_THERMISTOR = 2, ///< MCP9700 on ADS7952 multiplexed channel
    SENSOR_COUNT        ///< Number of sensor types (not a valid sensor)
};

/**
 * @brief Convert FluxTempSensorType to a human-readable string.
 * @param type Sensor type to convert.
 * @return Null-terminated string representation.
 */
constexpr const char* FluxTempSensorTypeToString(FluxTempSensorType type) noexcept {
    switch (type) {
        case FluxTempSensorType::ESP32_INTERNAL: return "ESP32 Internal";
        case FluxTempSensorType::NTC_THERMISTOR: return "NTC Thermistor";
        case FluxTempSensorType::MCP9700_THERMISTOR: return "MCP9700 Thermistor";
        default: return "Unknown";
    }
}

//==============================================================================
// TEMPERATURE ENTRY — stack-allocated metadata (drivers owned via unique_ptr)
//==============================================================================

/**
 * @brief A single temperature sensor entry in the fixed-size registry.
 */
struct TempEntry {
    std::string_view name;                   ///< Human-readable name
    FluxTempSensorType sensor_type{FluxTempSensorType::SENSOR_COUNT}; ///< Physical sensor type
    bool registered{false};                  ///< Whether this entry is active
    float last_reading_celsius{0.0f};        ///< Most recent cached reading in °C

    // Statistics
    uint32_t read_count{0};                  ///< Cumulative successful reads
    uint32_t error_count{0};                 ///< Cumulative failed reads
};

//==============================================================================
// TEMPERATURE DIAGNOSTICS
//==============================================================================

/**
 * @brief Temperature subsystem diagnostics snapshot (cross-HAL compatible format).
 */
struct TempSystemDiagnostics {
    bool system_healthy;                    ///< True when all registered sensors are responsive
    uint32_t total_sensors_registered;      ///< Number of sensors currently registered
    uint32_t sensors_by_type[8];            ///< Count of sensors per FluxTempSensorType index
    uint32_t total_operations;              ///< Cumulative read attempts across all sensors
    uint32_t successful_operations;         ///< Cumulative successful reads
    uint32_t failed_operations;             ///< Cumulative failed reads
    uint32_t communication_errors;          ///< Errors attributed to bus communication failures
    uint32_t hardware_errors;               ///< Errors attributed to hardware faults
    uint64_t system_uptime_ms;              ///< Milliseconds since temperature subsystem init
    TemperatureError last_error;            ///< Most recent error code from any operation
    float system_min_temp_celsius;          ///< Minimum temperature across all sensors (°C)
    float system_max_temp_celsius;          ///< Maximum temperature across all sensors (°C)
    float system_avg_temp_celsius;          ///< Average temperature across all sensors (°C)
};

//==============================================================================
// TEMPERATURE MANAGER
//==============================================================================

/**
 * @class TemperatureManager
 * @brief Singleton manager for all temperature sensors on Flux V1.
 *
 * @details Manages the ESP32 internal temperature sensor and the NTC
 *          thermistor connected via ADC (GPIO4 / ADC1_CH4). Sensors are
 *          registered in a fixed-size array at initialisation; readings
 *          are returned in Celsius or Fahrenheit.
 *
 * @note Call EnsureInitialized() once before any reads. All public methods
 *       are thread-safe (guarded by an internal RTOS mutex).
 */
class TemperatureManager {
public:
    //==========================================================================
    // SINGLETON AND LIFECYCLE
    //==========================================================================

    /** @brief Return the singleton TemperatureManager instance. */
    static TemperatureManager& GetInstance() noexcept;

    TemperatureManager(const TemperatureManager&) = delete;
    TemperatureManager& operator=(const TemperatureManager&) = delete;
    TemperatureManager(TemperatureManager&&) = delete;
    TemperatureManager& operator=(TemperatureManager&&) = delete;

    /**
     * @brief Initialise the temperature subsystem if not already initialised.
     * @return true on success or if already initialised.
     */
    [[nodiscard]] bool EnsureInitialized() noexcept;

    /**
     * @brief Check whether the temperature subsystem has been initialised.
     * @return true if initialised and ready for reads.
     */
    [[nodiscard]] bool IsInitialized() const noexcept { return initialized_.load(std::memory_order_acquire); }

    /**
     * @brief Release all temperature sensor resources and reset to uninitialized state.
     * @return true on success
     */
    [[nodiscard]] bool Deinitialize() noexcept;

    /**
     * @brief Get the most recent error code.
     * @return Last TemperatureError set by any API call
     */
    [[nodiscard]] TemperatureError GetLastError() const noexcept { return last_error_.load(std::memory_order_acquire); }

    //==========================================================================
    // TEMPERATURE READING
    //==========================================================================

    /**
     * @brief Read temperature in Celsius from a named sensor.
     * @param name           "ESP32_INTERNAL", "NTC_THERMISTOR", or "MCP9700_THERMISTOR"
     * @param temperature_c  Output temperature in °C
     * @return TemperatureError::SUCCESS on success
     */
    [[nodiscard]] TemperatureError ReadTemperatureCelsius(std::string_view name, float& temperature_c) noexcept;

    /**
     * @brief Read temperature in Fahrenheit from a named sensor.
     * @param name          Sensor name ("ESP32_INTERNAL" or "NTC_THERMISTOR").
     * @param temperature_f Output temperature in °F.
     * @return TemperatureError::SUCCESS on success.
     */
    [[nodiscard]] TemperatureError ReadTemperatureFahrenheit(std::string_view name, float& temperature_f) noexcept;

    /**
     * @brief Convenience: read the ESP32 internal temperature sensor.
     * @param temperature_c Output temperature in °C.
     * @return TemperatureError::SUCCESS on success.
     */
    [[nodiscard]] TemperatureError ReadInternalTemperature(float& temperature_c) noexcept;

    /**
     * @brief Convenience: read the NTC thermistor temperature.
     * @param temperature_c Output temperature in °C.
     * @return TemperatureError::SUCCESS on success.
     */
    [[nodiscard]] TemperatureError ReadNtcTemperature(float& temperature_c) noexcept;

    /**
     * @brief Convenience: read the MCP9700 sensor (ADS7952 CH8).
     * @param temperature_c Output temperature in °C.
     * @return TemperatureError::SUCCESS on success.
     */
    [[nodiscard]] TemperatureError ReadMcp9700Temperature(float& temperature_c) noexcept;

    /**
     * @brief Read all sensors and compute min, max, and average temperatures.
     * @param[out] min_c Minimum temperature across all sensors (°C).
     * @param[out] max_c Maximum temperature across all sensors (°C).
     * @param[out] avg_c Average temperature across all sensors (°C).
     * @return TemperatureError::SUCCESS on success.
     */
    [[nodiscard]] TemperatureError GetSystemTemperatureStats(float& min_c, float& max_c, float& avg_c) noexcept;

    //==========================================================================
    // SENSOR INFO
    //==========================================================================

    /** @brief Get the number of registered temperature sensors. */
    [[nodiscard]] size_t GetSensorCount() const noexcept { return registered_count_; }

    /**
     * @brief Look up a sensor entry by name.
     * @param name Sensor name.
     * @return Pointer to the TempEntry, or nullptr if not found.
     */
    [[nodiscard]] const TempEntry* GetSensorEntry(std::string_view name) const noexcept;

    //==========================================================================
    // DIAGNOSTICS
    //==========================================================================

    /**
     * @brief Populate a TempSystemDiagnostics snapshot.
     * @param[out] diag Diagnostics structure to fill.
     * @return TemperatureError::SUCCESS on success.
     */
    [[nodiscard]] TemperatureError GetSystemDiagnostics(TempSystemDiagnostics& diag) const noexcept;

    /** @brief Log temperature sensor statistics and health info to the console. */
    void DumpStatistics() const noexcept;

private:
    TemperatureManager() noexcept = default;
    ~TemperatureManager() = default;

    bool Initialize() noexcept;
    bool InitializeEsp32Internal() noexcept;
    bool InitializeNtcThermistor() noexcept;
    bool InitializeMcp9700Thermistor() noexcept;

    [[nodiscard]] size_t FindByName(std::string_view name) const noexcept;

    //==========================================================================
    // STORAGE
    //==========================================================================

    static constexpr size_t kMaxSensors = static_cast<size_t>(FluxTempSensorType::SENSOR_COUNT);
    static constexpr size_t kInvalidIndex = SIZE_MAX;

    std::array<TempEntry, kMaxSensors> entries_{};
    size_t registered_count_{0};
    uint64_t init_time_ms_{0};  ///< Timestamp (ms) when Initialize() completed

    // Sensor driver instances. The MCU die sensor is owned by the platform binding;
    // the handlers are heap-allocated via unique_ptr at init (singleton lifetime).
    BaseTemperature* die_temp_{nullptr};
    std::unique_ptr<NtcTemperatureHandler> ntc_handler_;
    std::unique_ptr<Mcp9700TemperatureHandler> mcp9700_handler_;

    std::atomic<bool> initialized_{false};
    std::atomic<TemperatureError> last_error_{TemperatureError::SUCCESS};
    mutable RtosMutex mutex_;
};

//==============================================================================
// CONVENIENCE
//==============================================================================

/**
 * @brief Convenience accessor — equivalent to TemperatureManager::GetInstance().
 * @return Reference to the singleton TemperatureManager.
 */
[[nodiscard]] inline TemperatureManager& GetTemperatureManager() noexcept {
    return TemperatureManager::GetInstance();
}

#endif // FLUX_TEMPERATURE_MANAGER_H_
