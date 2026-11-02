/**
 * @file FluxPlatformBind.h
 * @brief The MCU-family seam of the Flux V1 HAL: what a binding must provide to the managers.
 *
 * @details The managers above this header are MCU-free: they decide *what* to bring up (which pins,
 *          which buses, which namespaces, in which order, and what a failure means) and talk to the
 *          result through hf-core `Base*` interfaces. This header names the small set of factory and
 *          system functions that turn a plain-data spec into a `Base*` object. Each MCU family
 *          implements every function once, in its own directory:
 *
 *          | Directory              | Built when                         | Backs the objects with              |
 *          |------------------------|------------------------------------|-------------------------------------|
 *          | `bsp_bind/esp32s3/`    | ESP-IDF build (`HF_CORE_MCU=ESP32`) | `Esp*` wrappers, ESP-IDF, RMT driver |
 *          | `bsp_bind/host/`       | host test build                    | hf-core host mocks                  |
 *
 *          Exactly one family is linked into an image. ESP-IDF, FreeRTOS and `Esp*` headers may appear
 *          only under `bsp_bind/<family>/` (enforced by `tools/lint/lint_hal_boundary.sh`).
 *
 * Ownership: every object returned here lives in fixed static storage inside the binding (one slot
 *            per pin, bus, port or region). Nothing is heap-allocated by the binding itself; ESP-IDF
 *            drivers that allocate internally do so inside the `Create*` call, i.e. during the owning
 *            manager's `EnsureInitialized()`.
 *
 * Callers: managers only (the `*Manager.cpp` files in `lib/managers`) and the `Flux` facade for the
 *          system calls.
 *          Applications never include this header.
 *
 * Thread safety: none inside the binding. Each slot has exactly one owning manager, which serialises
 *                its own calls (manager mutex or init-time only).
 *
 * Error handling: `Create*` returns `nullptr` / `false` on failure and leaves the slot empty; the
 *                 calling manager records the error. `Destroy*` on an empty slot is a no-op.
 *
 * @author HardFOC Team
 * @date 2026
 */

#ifndef FLUX_BSP_PLATFORM_BIND_H_
#define FLUX_BSP_PLATFORM_BIND_H_

#include <cstddef>
#include <cstdint>

#include "core/hf-core-drivers/internal/hf-internal-interface-wrap/inc/base/BaseAdc.h"
#include "core/hf-core-drivers/internal/hf-internal-interface-wrap/inc/base/BaseCan.h"
#include "core/hf-core-drivers/internal/hf-internal-interface-wrap/inc/base/BaseGpio.h"
#include "core/hf-core-drivers/internal/hf-internal-interface-wrap/inc/base/BaseNvs.h"
#include "core/hf-core-drivers/internal/hf-internal-interface-wrap/inc/base/BaseSpi.h"
#include "core/hf-core-drivers/internal/hf-internal-interface-wrap/inc/base/BaseTemperature.h"
#include "core/hf-core-drivers/internal/hf-internal-interface-wrap/inc/base/BaseUart.h"
#include "core/hf-core-drivers/internal/hf-internal-interface-wrap/inc/base/BaseUsbSerialJtag.h"

namespace flux::bsp {

//==============================================================================
// CAPACITIES — every binding sizes its static storage from these
//==============================================================================

inline constexpr size_t kMaxGpioSlots          = 64;  ///< GPIO objects (one per functional pin index)
inline constexpr size_t kMaxSpiBuses           = 2;   ///< SPI controllers in use
inline constexpr size_t kMaxSpiDevicesPerBus   = 4;   ///< Chip selects per SPI bus
inline constexpr size_t kMaxUartPorts          = 3;   ///< Hardware UART ports
inline constexpr size_t kMaxNvsRegions         = 3;   ///< NVS namespaces opened by NvsManager
inline constexpr uint8_t kMaxInternalAdcChannels = 16; ///< Upper bound on MCU ADC channels per unit

//==============================================================================
// GPIO
//==============================================================================

/** @brief Electrical configuration of one MCU GPIO. */
struct GpioSpec {
    hf_pin_num_t           pin;           ///< MCU pin number
    hf_gpio_direction_t    direction;     ///< Direction at creation (applied on first init)
    hf_gpio_active_state_t active_state;  ///< Logical active level
    hf_gpio_output_mode_t  output_mode;   ///< Push-pull or open-drain
    hf_gpio_pull_mode_t    pull_mode;     ///< Internal pull resistor
};

/**
 * @brief Create the GPIO object for @p slot. No hardware access until the object is initialised.
 * @param slot Index below @ref kMaxGpioSlots (the functional pin index).
 * @param spec Pin and electrical configuration.
 * @return The object, or nullptr if @p slot is out of range or already in use.
 */
BaseGpio* CreateGpio(size_t slot, const GpioSpec& spec) noexcept;

/** @brief Destroy the GPIO object in @p slot (releases the pin). */
void DestroyGpio(size_t slot) noexcept;

//==============================================================================
// MCU ADC AND DIE TEMPERATURE
//==============================================================================

/** @brief Number of channels per unit of the MCU ADC (0 when the family has none). */
uint8_t InternalAdcChannelCount() noexcept;

/**
 * @brief Create and initialise the MCU ADC unit with the given channels enabled.
 * @param unit         ADC unit (0 = ADC1).
 * @param channel_mask Bit n set = enable channel n (full-scale range, 12-bit).
 * @return The ADC, or nullptr if the unit failed to initialise (the slot is then empty).
 */
BaseAdc* CreateInternalAdc(uint8_t unit, uint32_t channel_mask) noexcept;

/** @brief Destroy the MCU ADC unit. */
void DestroyInternalAdc() noexcept;

/** @brief Create the MCU die-temperature sensor (not initialised). nullptr if the family has none. */
BaseTemperature* CreateDieTemperature() noexcept;

/** @brief Destroy the die-temperature sensor. */
void DestroyDieTemperature() noexcept;

//==============================================================================
// NVS
//==============================================================================

/**
 * @brief The NVS region in @p slot, created on first call for @p name_space (no flash access).
 * @param slot       Index below @ref kMaxNvsRegions.
 * @param name_space Namespace name (at most 15 characters); ignored after the first call.
 * @return Reference valid for the life of the program.
 */
BaseNvs& NvsRegion(size_t slot, const char* name_space) noexcept;

//==============================================================================
// SPI
//==============================================================================

/** @brief One SPI controller and its pins. */
struct SpiBusSpec {
    uint8_t      controller;  ///< Family controller index (on ESP32-S3: 1 = SPI2_HOST, 2 = SPI3_HOST)
    hf_pin_num_t miso;        ///< MISO pin
    hf_pin_num_t mosi;        ///< MOSI pin
    hf_pin_num_t sclk;        ///< SCLK pin
    bool         use_iomux;   ///< Route through the IO MUX (true) or the GPIO matrix (false)
    uint32_t     timeout_ms;  ///< Transfer timeout
};

/** @brief One chip select on an SPI bus. */
struct SpiDeviceSpec {
    hf_pin_num_t cs;          ///< Chip-select pin
    uint32_t     clock_hz;    ///< SCLK frequency
    uint8_t      mode;        ///< SPI mode 0..3 (CPOL/CPHA)
};

/**
 * @brief Create and initialise SPI bus @p bus.
 * @return true when the controller is up.
 */
bool CreateSpiBus(size_t bus, const SpiBusSpec& spec) noexcept;

/**
 * @brief Add a device to an initialised SPI bus.
 * @return The device, or nullptr if the bus is down or the device could not be added.
 */
BaseSpi* CreateSpiDevice(size_t bus, const SpiDeviceSpec& spec) noexcept;

/** @brief Destroy SPI bus @p bus and every device on it. */
void DestroySpiBus(size_t bus) noexcept;

//==============================================================================
// CAN
//==============================================================================

/** @brief The CAN controller and its pins. */
struct CanSpec {
    hf_pin_num_t tx;          ///< TX pin
    hf_pin_num_t rx;          ///< RX pin
    uint32_t     bitrate;     ///< Nominal bit rate (bit/s)
};

/** @brief Create the CAN controller (configured, not started). nullptr on failure. */
BaseCan* CreateCan(const CanSpec& spec) noexcept;

/** @brief Destroy the CAN controller. */
void DestroyCan() noexcept;

//==============================================================================
// UART AND USB SERIAL/JTAG
//==============================================================================

/** @brief One hardware UART, 8N1, no flow control. */
struct UartSpec {
    uint8_t      port;            ///< Port number below @ref kMaxUartPorts
    hf_pin_num_t tx;              ///< TX pin (-1 = leave unassigned)
    hf_pin_num_t rx;              ///< RX pin (-1 = leave unassigned)
    hf_pin_num_t rts;             ///< RTS / RS-485 DE pin (-1 = plain UART)
    hf_pin_num_t cts;             ///< CTS pin (-1 = none)
    uint32_t     baud;            ///< Baud rate
    uint16_t     tx_buffer_size;  ///< Driver TX ring size (bytes)
    uint16_t     rx_buffer_size;  ///< Driver RX ring size (bytes)
};

/**
 * @brief Create and initialise a UART. When @p spec.rts >= 0 the port is switched to RS-485 half duplex.
 * @param spec       Port and line configuration.
 * @param[out] rs485 Set to true when RS-485 mode was requested and applied (may be nullptr).
 * @return The UART, or nullptr if the port failed to initialise.
 */
BaseUart* CreateUart(const UartSpec& spec, bool* rs485) noexcept;

/** @brief Destroy the UART on @p port. */
void DestroyUart(uint8_t port) noexcept;

/**
 * @brief The USB Serial/JTAG controller, initialised. nullptr when the family has none or the
 *        feature is compiled out (`HF_CORE_ENABLE_USB_SERIAL_JTAG=OFF`). First call wins @p cfg.
 */
BaseUsbSerialJtag* AcquireUsbSerialJtag(const hf_usb_serial_jtag_config_t& cfg) noexcept;

//==============================================================================
// STATUS LED STRIP
//==============================================================================

/** @brief An addressable RGB LED strip on one data pin. */
struct LedStripSpec {
    hf_pin_num_t pin;         ///< Data pin
    uint8_t      channel;     ///< Peripheral channel (RMT on ESP32-S3)
    uint16_t     count;       ///< LEDs on the strip
};

/** @brief Create and start the strip. @return true when it is ready. */
bool LedStripBegin(const LedStripSpec& spec) noexcept;

/** @brief Stage a 0xRRGGBB colour for LED @p index (sent on @ref LedStripShow). */
void LedStripSetPixel(uint16_t index, uint32_t rgb) noexcept;

/** @brief Global brightness 0..255 applied to the next @ref LedStripShow. */
void LedStripSetBrightness(uint8_t brightness) noexcept;

/** @brief Send the staged colours. @return true on success. */
bool LedStripShow() noexcept;

/** @brief Stop the strip and release its peripheral. */
void LedStripEnd() noexcept;

//==============================================================================
// SYSTEM
//==============================================================================

/** @brief Opaque reset cause of the previous boot (family-specific numbering; 0 when unknown). */
uint32_t LastResetReason() noexcept;

/** @brief Software reset of the MCU. Does not return. */
[[noreturn]] void SystemRestart() noexcept;

/** @brief Free bytes in the default heap (0 when unknown). */
uint32_t FreeHeapBytes() noexcept;

/** @brief Largest contiguous free block in the default heap (0 when unknown). */
uint32_t LargestFreeHeapBlock() noexcept;

}  // namespace flux::bsp

#endif  // FLUX_BSP_PLATFORM_BIND_H_
