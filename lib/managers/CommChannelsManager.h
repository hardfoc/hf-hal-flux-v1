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
#include "core/hf-core-drivers/internal/hf-internal-interface-wrap/inc/utils/RtosMutex.h"

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
    SUCCESS = 0,
    NOT_INITIALIZED,
    INITIALIZATION_FAILED,
    BUS_NOT_AVAILABLE,
    DEVICE_NOT_FOUND,
    INVALID_BUS_INDEX,
    INVALID_DEVICE_INDEX,
    DEVICE_ALREADY_EXISTS,
    DEVICE_CREATION_FAILED,
    COMMUNICATION_FAILED,
    INVALID_PARAMETER,
    MUTEX_LOCK_FAILED
};

/** @brief Convert CommError to string for debugging. */
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
// SPI DEVICE IDENTIFIERS
//==============================================================================

/**
 * @brief SPI device identifiers for the Flux V1 board.
 * @details The order matches the CS pin configuration in the CommChannelsManager.
 */
enum class SpiDeviceId : uint8_t {
    MAX22200_SOLENOID_DRIVER = 0,   ///< MAX22200 octal solenoid driver (SPI Mode 0)
    TLE92466_LOWSIDE_DRIVER = 1,    ///< TLE92466ED six-channel driver (SPI Mode 1)
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
    static CommChannelsManager& GetInstance() noexcept;

    // Non-copyable, non-movable
    CommChannelsManager(const CommChannelsManager&) = delete;
    CommChannelsManager& operator=(const CommChannelsManager&) = delete;
    CommChannelsManager(CommChannelsManager&&) = delete;
    CommChannelsManager& operator=(CommChannelsManager&&) = delete;

    //==========================================================================
    // INITIALIZATION
    //==========================================================================

    bool EnsureInitialized() noexcept;
    [[nodiscard]] bool IsInitialized() const noexcept { return initialized_.load(std::memory_order_acquire); }

    /**
     * @brief Release all bus resources and reset to uninitialized state.
     * @return true on success
     */
    bool Deinitialize() noexcept;

    //==========================================================================
    // SPI ACCESSORS
    //==========================================================================

    /** @brief Get SPI device by enum id. Returns nullptr on bad id or uninit. */
    BaseSpi* GetSpiDevice(SpiDeviceId device_id) noexcept;

    /** @brief Get SPI device by raw index (0-based). */
    BaseSpi* GetSpiDevice(uint8_t device_index) noexcept;

    //==========================================================================
    // I2C ACCESSOR (for future expansion)
    //==========================================================================

    /** @brief Get the primary I2C bus. Returns nullptr if not initialized. */
    EspI2cBus* GetI2cBus() noexcept;

    //==========================================================================
    // CAN ACCESSOR
    //==========================================================================

    /** @brief Get the TWAI/CAN bus. Returns nullptr if not initialized. */
    BaseCan* GetCanBus() noexcept;

    //==========================================================================
    // DIAGNOSTICS
    //==========================================================================

    void DumpStatistics() const noexcept;

private:
    CommChannelsManager() noexcept;
    ~CommChannelsManager();

    bool Initialize() noexcept;

    //==========================================================================
    // HARDWARE RESOURCES — heap-allocated via unique_ptr (singleton lifetime)
    //==========================================================================

    static constexpr uint8_t kSpiDeviceCount = static_cast<uint8_t>(SpiDeviceId::DEVICE_COUNT);

    // SPI bus + device indices (populated by EspSpiBus)
    std::unique_ptr<EspSpiBus> spi_bus_;
    std::array<int, kSpiDeviceCount> spi_device_indices_{};
    bool spi_bus_valid_{false};

    // I2C bus placeholder for future expansion
    std::unique_ptr<EspI2cBus> i2c_bus_;
    bool i2c_bus_valid_{false};

    // CAN bus
    std::unique_ptr<EspCan> can_bus_;
    bool can_bus_valid_{false};

    std::atomic<bool> initialized_{false};
    mutable RtosMutex mutex_;
};

#endif // FLUX_COMM_CHANNELS_MANAGER_H_
