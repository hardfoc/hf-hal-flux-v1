/**
 * @file CommChannelsManager.h
 * @brief Communication channels manager for the HardFOC Flux V1 platform.
 *
 * @details Manages all board communication buses (SPI, I2C, CAN) for the Flux
 *          valve-control board. The Flux board uses SPI to talk to:
 *            - MAX22200 octal solenoid driver
 *            - TLE92466ED six-channel low-side driver
 *          And I2C/CAN for future expansion.
 *
 *          Stack-based where possible: fixed-size arrays for known compile-time
 *          device counts, no heap vectors for bus/device lists. Bus objects
 *          are individually heap-allocated via unique_ptr (singleton lifetime).
 *
 * @author HardFOC Team
 * @date 2026
 * @version 1.0
 */

#ifndef FLUX_COMM_CHANNELS_MANAGER_H_
#define FLUX_COMM_CHANNELS_MANAGER_H_

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>

// Forward declarations
class EspSpiBus;
class EspSpiDevice;
class EspI2cBus;
class EspCan;
class BaseSpi;
class BaseI2c;
class BaseCan;

#include "core/hf-core-drivers/internal/hf-internal-interface-wrap/inc/base/BaseSpi.h"
#include "core/hf-core-drivers/internal/hf-internal-interface-wrap/inc/base/BaseI2c.h"
#include "core/hf-core-drivers/internal/hf-internal-interface-wrap/inc/base/BaseCan.h"
#include "RtosMutex.h"

//==============================================================================
// COMM CHANNELS ERROR CODES
//==============================================================================

/**
 * @brief Communication channels manager error codes for consistent error reporting.
 *
 * Manager-layer errors for communication bus lifecycle and device operations.
 * Covers SPI, I2C, and CAN bus management failures.
 */
enum class CommError : uint8_t {
    SUCCESS = 0,              ///< Operation completed successfully
    NOT_INITIALIZED,          ///< Manager not yet initialised
    INITIALIZATION_FAILED,    ///< Bus or device initialisation failed
    BUS_NOT_AVAILABLE,        ///< Requested bus is not present or not ready
    DEVICE_NOT_FOUND,         ///< Device index or id not in registry
    INVALID_BUS_INDEX,        ///< Bus index out of range
    INVALID_DEVICE_INDEX,     ///< Device index out of range
    DEVICE_ALREADY_EXISTS,    ///< A device at that address already exists
    DEVICE_CREATION_FAILED,   ///< Heap allocation or driver init for device failed
    COMMUNICATION_FAILED,     ///< A bus-level transfer failed
    INVALID_PARAMETER,        ///< Null pointer or out-of-range argument
    MUTEX_LOCK_FAILED         ///< RTOS mutex acquire timed out
};

/**
 * @brief Convert CommError to a human-readable string.
 * @param error Error code to convert.
 * @return Null-terminated string representation.
 */
constexpr const char* CommErrorToString(CommError error) noexcept {
    switch (error) {
        case CommError::SUCCESS:                return "Success";
        case CommError::NOT_INITIALIZED:        return "Not initialized";
        case CommError::INITIALIZATION_FAILED:  return "Initialization failed";
        case CommError::BUS_NOT_AVAILABLE:      return "Bus not available";
        case CommError::DEVICE_NOT_FOUND:       return "Device not found";
        case CommError::INVALID_BUS_INDEX:      return "Invalid bus index";
        case CommError::INVALID_DEVICE_INDEX:   return "Invalid device index";
        case CommError::DEVICE_ALREADY_EXISTS:  return "Device already exists";
        case CommError::DEVICE_CREATION_FAILED: return "Device creation failed";
        case CommError::COMMUNICATION_FAILED:   return "Communication failed";
        case CommError::INVALID_PARAMETER:      return "Invalid parameter";
        case CommError::MUTEX_LOCK_FAILED:      return "Mutex lock failed";
        default:                                return "Unknown error";
    }
}

//==============================================================================
// COMM CHANNELS DIAGNOSTICS
//==============================================================================

/**
 * @brief System-level diagnostics snapshot for communication channels.
 */
struct CommSystemDiagnostics {
    bool system_healthy;            ///< True when all configured buses are operational
    bool system_initialized;        ///< True if EnsureInitialized() completed
    bool spi_bus_valid;             ///< SPI bus initialised and usable
    bool i2c_bus_valid;             ///< I²C bus initialised and usable
    bool can_bus_valid;             ///< CAN/TWAI bus initialised and usable
    uint8_t spi_device_count;       ///< Number of SPI devices with valid indices
    CommError last_error;           ///< Most recent error code from any operation
};

//==============================================================================
// SPI DEVICE IDENTIFIERS
//==============================================================================

/**
 * @brief SPI device identifiers for the Flux V1 board.
 * @details SPI2 bus: MAX22200 + TLE92466ED (shared bus, valves)
 *          SPI3 bus: ADS7952 ADC devices (dedicated bus, external ADC)
 */
enum class SpiDeviceId : uint8_t {
    MAX22200_SOLENOID_DRIVER = 0,   ///< MAX22200 octal solenoid driver (SPI2, Mode 0)
    TLE92466_LOWSIDE_DRIVER = 1,    ///< TLE92466ED six-channel driver (SPI2, Mode 1)
    ADS7952_ADC_0 = 2,              ///< ADS7952 12-ch ADC device 0 (SPI3, Mode 0)
    DEVICE_COUNT                     ///< Sentinel — total number of SPI devices
};

//==============================================================================
// COMM CHANNELS MANAGER
//==============================================================================

/**
 * @class CommChannelsManager
 * @brief Singleton managing all board communication channels for Flux V1.
 *
 * Design decisions vs. Vortex:
 *  - Fixed compile-time device counts (no heap vectors for device tracking).
 *  - SPI bus + two devices known at link time.
 *  - I2C bus kept for future expansion but no devices configured yet.
 *  - CAN bus available for valve network communication.
 */
class CommChannelsManager {
public:
    /** @brief Return the singleton CommChannelsManager instance. */
    static CommChannelsManager& GetInstance() noexcept;

    // Non-copyable, non-movable
    CommChannelsManager(const CommChannelsManager&) = delete;
    CommChannelsManager& operator=(const CommChannelsManager&) = delete;
    CommChannelsManager(CommChannelsManager&&) = delete;
    CommChannelsManager& operator=(CommChannelsManager&&) = delete;

    //==========================================================================
    // INITIALIZATION
    //==========================================================================

    /**
     * @brief Initialise all communication buses if not already initialised.
     * @return true on success or if already initialised.
     */
    bool EnsureInitialized() noexcept;

    /**
     * @brief Check whether the communication subsystem has been initialised.
     * @return true if initialised.
     */
    [[nodiscard]] bool IsInitialized() const noexcept { return initialized_.load(std::memory_order_acquire); }

    /**
     * @brief Get the most recent error code.
     * @return Last CommError set by any API call
     */
    [[nodiscard]] CommError GetLastError() const noexcept { return last_error_.load(std::memory_order_acquire); }

    /**
     * @brief Fill a diagnostics snapshot with current system state.
     * @param diagnostics Output structure to populate
     * @return CommError::SUCCESS on success, CommError::NOT_INITIALIZED if not init
     */
    [[nodiscard]] CommError GetSystemDiagnostics(CommSystemDiagnostics& diagnostics) const noexcept;

    /**
     * @brief Release all bus resources and reset to uninitialized state.
     * @return true on success
     */
    bool Deinitialize() noexcept;

    //==========================================================================
    // SPI ACCESSORS
    //==========================================================================

    /**
     * @brief Get an SPI device handle by enum identifier.
     * @param device_id SPI device identifier.
     * @return Pointer to BaseSpi, or nullptr on invalid id or uninitialised.
     */
    BaseSpi* GetSpiDevice(SpiDeviceId device_id) noexcept;

    /**
     * @brief Get an SPI device handle by raw index.
     * @param device_index Zero-based device index.
     * @return Pointer to BaseSpi, or nullptr on invalid index or uninitialised.
     */
    BaseSpi* GetSpiDevice(uint8_t device_index) noexcept;

    //==========================================================================
    // I2C ACCESSOR (for future expansion)
    //==========================================================================

    /**
     * @brief Get the primary I²C bus (for future expansion).
     * @return Pointer to EspI2cBus, or nullptr if not initialised.
     */
    EspI2cBus* GetI2cBus() noexcept;

    //==========================================================================
    // CAN ACCESSOR
    //==========================================================================

    /**
     * @brief Get the TWAI/CAN bus.
     * @return Pointer to BaseCan, or nullptr if not initialised.
     */
    BaseCan* GetCanBus() noexcept;

    //==========================================================================
    // DIAGNOSTICS
    //==========================================================================

    /** @brief Log communication channel statistics and bus health to the console. */
    void DumpStatistics() const noexcept;

private:
    CommChannelsManager() noexcept;
    ~CommChannelsManager();

    bool Initialize() noexcept;

    //==========================================================================
    // HARDWARE RESOURCES — heap-allocated via unique_ptr (singleton lifetime)
    //==========================================================================

    static constexpr uint8_t kSpiDeviceCount = static_cast<uint8_t>(SpiDeviceId::DEVICE_COUNT);

    // SPI2 bus (valve drivers: MAX22200 + TLE92466ED)
    std::unique_ptr<EspSpiBus> spi_bus_;
    bool spi_bus_valid_{false};

    // SPI3 bus (ADS7952 external ADC — dedicated bus)
    std::unique_ptr<EspSpiBus> spi3_bus_;
    bool spi3_bus_valid_{false};

    // Device indices: tracks device across both buses
    std::array<int, kSpiDeviceCount> spi_device_indices_{};
    // Which bus owns each device (0=spi_bus_, 1=spi3_bus_)
    std::array<uint8_t, kSpiDeviceCount> spi_device_bus_{};

    // I2C bus placeholder for future expansion
    std::unique_ptr<EspI2cBus> i2c_bus_;
    bool i2c_bus_valid_{false};

    // CAN bus
    std::unique_ptr<EspCan> can_bus_;
    bool can_bus_valid_{false};

    std::atomic<bool> initialized_{false};
    std::atomic<CommError> last_error_{CommError::SUCCESS};
    mutable RtosMutex mutex_;

    void UpdateLastError(CommError error) noexcept {
        last_error_.store(error, std::memory_order_release);
    }

    /** @brief Resolve the bus pointer for a device index. */
    EspSpiBus* GetBusForDevice(uint8_t device_index) noexcept;
};

//==============================================================================
// CONVENIENCE
//==============================================================================

/**
 * @brief Convenience accessor — equivalent to CommChannelsManager::GetInstance().
 * @return Reference to the singleton CommChannelsManager.
 */
[[nodiscard]] inline CommChannelsManager& GetCommChannelsManager() noexcept {
    return CommChannelsManager::GetInstance();
}

#endif // FLUX_COMM_CHANNELS_MANAGER_H_
