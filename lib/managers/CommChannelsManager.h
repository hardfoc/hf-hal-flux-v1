/**
 * @file CommChannelsManager.h
 * @brief Communication channels manager for the HardFOC Flux V1 platform.
 *
 * @details Manages board communication buses (SPI, CAN, lazy UART) for the Flux
 *          valve-control board. SPI talks to:
 *            - MAX22200 octal solenoid driver
 *            - TLE92466ED six-channel low-side driver
 *          CAN is used for external devices (e.g. compressor). There is no
 *          I²C bus on Flux V1 — `GetI2cBus()` always returns nullptr.
 *
 *          The bus objects are created by the platform binding
 *          (`bsp_bind/FluxPlatformBind.h`) in its static storage; this manager
 *          decides which buses and chip selects exist (pin map + board config)
 *          and holds `Base*` pointers only, so this header is MCU-free.
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

// Forward declaration — only appears as the return type of GetI2cBus()
class BaseI2c;

#include "core/hf-core-drivers/internal/hf-internal-interface-wrap/inc/base/BaseSpi.h"
#include "core/hf-core-drivers/internal/hf-internal-interface-wrap/inc/base/BaseCan.h"
#include "core/hf-core-drivers/internal/hf-internal-interface-wrap/inc/base/BaseUart.h"
#include "core/hf-core-drivers/internal/hf-internal-interface-wrap/inc/base/BaseUsbSerialJtag.h"
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
    bool i2c_bus_valid;             ///< Always false — Flux V1 has no I²C manager bus
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
 *  - No I²C bus (GPIO not routed / unused on this product).
 *  - CAN bus available for external controllers (e.g. compressor).
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
    // I2C — not present on Flux V1
    //==========================================================================

    /**
     * @brief Flux V1 does not bring up an I²C bus in `CommChannelsManager`.
     * @return Always nullptr (API retained for portable example code).
     */
    BaseI2c* GetI2cBus() noexcept { return nullptr; }

    //==========================================================================
    // CAN ACCESSOR
    //==========================================================================

    /**
     * @brief Get the TWAI/CAN bus.
     * @return Pointer to BaseCan, or nullptr if not initialised.
     */
    BaseCan* GetCanBus() noexcept;

    //==========================================================================
    // UART ACCESSORS
    //==========================================================================

    /**
     * @brief Lazy-acquire configuration for a hardware UART port.
     *
     * Apps that own a serial sensor (BASIS-2, FDO2, …) supply this to
     * `AcquireUart()`. The HAL owns the port; the app owns the device
     * driver layered on top of the returned `BaseUart&`.
     */
    struct UartAcquireConfig {
        uint8_t  port_number;       ///< Hardware UART port (0/1/2)
        int16_t  tx_pin;            ///< GPIO; -1 to skip pin assignment.
        int16_t  rx_pin;            ///< GPIO; -1 to skip pin assignment.
        int16_t  rts_pin;           ///< -1 = plain UART, else RS-485 DE/RE.
        int16_t  cts_pin;           ///< -1 = no CTS (typical).
        uint32_t baud_bps;
        uint16_t tx_buffer_size;
        uint16_t rx_buffer_size;
    };

    /// Maximum number of UART ports the manager will track (one slot per
    /// ESP32-S3 hardware UART; UART0 is normally the IDF console, UART2
    /// the operator console — pick a free port for app use).
    static constexpr uint8_t kMaxUartPorts = 3;

    /**
     * @brief Acquire (lazy-create + cache) a UART on the given hardware port.
     *
     * First call wins the configuration; subsequent calls with the same
     * `port_number` return the cached `BaseUart&` and ignore further
     * config requests (a warning is logged if the new config differs from
     * what was cached). Returns nullptr on configuration error.
     *
     * RS-485 half-duplex is enabled automatically when `rts_pin >= 0`.
     */
    BaseUart* AcquireUart(const UartAcquireConfig& cfg) noexcept;

    /**
     * @brief Look up a previously-acquired UART by port number.
     * @return Pointer to the cached BaseUart, or nullptr
     *         if no UART has been acquired on that port yet.
     */
    BaseUart* GetUart(uint8_t port_number) noexcept;

    //==========================================================================
    // USB SERIAL/JTAG ACCESSORS (ESP32-S3/-C3/-C6 native USB-VCP + JTAG)
    //==========================================================================

    /**
     * @brief Lazy-acquire the built-in USB Serial/JTAG controller.
     *
     * The ESP32-S3 has exactly one USB Serial/JTAG block, hard-wired to
     * GPIO19/20 — there are no pins or baud rate to configure, only the
     * driver-side TX/RX ring sizes. The first caller wins the ring sizes;
     * subsequent calls reuse the same `BaseUsbSerialJtag&`.
     *
     * If the IDF console (`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`) has
     * already installed the underlying driver, this just shares it — both
     * `printf` / `ESP_LOG` and our wrapper push to the same TX ring.
     *
     * @param cfg Ring buffer / policy configuration (defaults are sane:
     *            256-byte TX, 256-byte RX). Honoured only on the first call.
     * @return Pointer to the cached `BaseUsbSerialJtag`,
     *         or nullptr if the host platform has no USB Serial/JTAG block
     *         or driver install failed.
     */
    BaseUsbSerialJtag* AcquireUsbSerialJtag(
        const hf_usb_serial_jtag_config_t& cfg = {}) noexcept;

    /**
     * @brief Look up a previously-acquired USB Serial/JTAG instance.
     * @return Pointer to the cached `BaseUsbSerialJtag`, or nullptr if
     *         `AcquireUsbSerialJtag()` has never been called.
     */
    BaseUsbSerialJtag* GetUsbSerialJtag() noexcept;

    //==========================================================================
    // DIAGNOSTICS
    //==========================================================================

    /** @brief Log communication channel statistics and bus health to the console. */
    void DumpStatistics() const noexcept;

private:
    CommChannelsManager() noexcept = default;
    ~CommChannelsManager() = default;

    bool Initialize() noexcept;

    //==========================================================================
    // HARDWARE RESOURCES — objects live in the platform binding (bsp_bind);
    //                      the manager holds Base* pointers only
    //==========================================================================

    static constexpr uint8_t kSpiDeviceCount = static_cast<uint8_t>(SpiDeviceId::DEVICE_COUNT);

    bool spi_bus_valid_{false};    ///< Valve bus (MAX22200 + TLE92466ED) up
    bool spi3_bus_valid_{false};   ///< Pressure bus (ADS7952) up

    /// One device per SpiDeviceId; nullptr when its bus or chip select failed.
    std::array<BaseSpi*, kSpiDeviceCount> spi_devices_{};

    BaseCan* can_bus_{nullptr};    ///< CAN controller (configured, not started)
    bool can_bus_valid_{false};

    // Lazy UART pool — one slot per hardware UART port. App-driven:
    // sensors (BASIS-2, FDO2, etc.) call `AcquireUart(cfg)` at boot.
    std::array<BaseUart*, kMaxUartPorts> uarts_{};

    // Lazy USB Serial/JTAG. The controller is a single silicon block, owned by
    // the platform binding; we only cache the resolved pointer here.
    BaseUsbSerialJtag* usb_serial_jtag_{nullptr};

    std::atomic<bool> initialized_{false};
    std::atomic<CommError> last_error_{CommError::SUCCESS};
    mutable RtosMutex mutex_;

    void UpdateLastError(CommError error) noexcept {
        last_error_.store(error, std::memory_order_release);
    }
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
