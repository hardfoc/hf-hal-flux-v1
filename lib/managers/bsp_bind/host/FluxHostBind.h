/**
 * @file FluxHostBind.h
 * @brief Host (unit-test) family of the Flux platform binding: scripting and inspection hooks.
 *
 * @details `FluxPlatformBindHost.cpp` implements `bsp_bind/FluxPlatformBind.h` with the hf-core host
 *          fakes (`hf_core_host_mocks`): every GPIO is a `MockGpio`, every SPI chip select a `MockSpi`,
 *          the MCU ADC a `MockAdc`, NVS regions `MockNvs`, and so on, all in static slots exactly like
 *          the ESP32-S3 binding. This header lets a test reach those fakes, script them before a
 *          manager initialises, and inject failures. Only `tests/host` includes it.
 *
 * Thread safety: none; host tests are single-threaded.
 *
 * @author HardFOC Team
 * @date 2026
 */

#ifndef FLUX_BSP_HOST_BIND_H_
#define FLUX_BSP_HOST_BIND_H_

#include <cstddef>
#include <cstdint>

#include "HfCoreMocks.h"
#include "bsp_bind/FluxPlatformBind.h"

namespace flux::bsp::host {

/** @brief Destroy every object in every slot and clear all scripting (call between test cases). */
void Reset() noexcept;

// ── GPIO ──────────────────────────────────────────────────────────────────
/** @brief The fake in GPIO slot @p slot, or nullptr when the slot is empty. */
hf::host::MockGpio* Gpio(size_t slot) noexcept;
/** @brief The fake driving MCU pin @p pin, or nullptr. */
hf::host::MockGpio* GpioByPin(hf_pin_num_t pin) noexcept;
/** @brief The spec the slot was created with (valid only while the slot is in use). */
const GpioSpec* GpioSpecOf(size_t slot) noexcept;
/** @brief Make the next CreateGpio for @p pin produce a fake whose initialisation fails. */
void FailGpioInit(hf_pin_num_t pin) noexcept;

// ── SPI ───────────────────────────────────────────────────────────────────
/** @brief Install a responder for the device whose chip select is @p cs (applied when it is created). */
void SetSpiResponder(hf_pin_num_t cs, hf::host::MockSpi::Responder responder);
/** @brief The fake for chip select @p cs, or nullptr when no such device exists. */
hf::host::MockSpi* SpiDeviceByCs(hf_pin_num_t cs) noexcept;
/** @brief The spec the device on chip select @p cs was created with, or nullptr. */
const SpiDeviceSpec* SpiDeviceSpecOf(hf_pin_num_t cs) noexcept;
/** @brief The spec bus @p bus was created with, or nullptr when it is down. */
const SpiBusSpec* SpiBusSpecOf(size_t bus) noexcept;
/** @brief Make CreateSpiBus(@p bus) fail. */
void FailSpiBus(size_t bus, bool fail = true) noexcept;

// ── MCU ADC, die temperature, NVS, CAN, UART, LED ─────────────────────────
/** @brief The MCU ADC fake, or nullptr when not created. */
hf::host::MockAdc* InternalAdc() noexcept;
/** @brief Channel mask the MCU ADC was created with. */
uint32_t InternalAdcChannelMask() noexcept;
/** @brief Make CreateInternalAdc fail. */
void FailInternalAdc(bool fail = true) noexcept;
/** @brief The die-temperature fake, or nullptr. */
hf::host::MockTemperature* DieTemperature() noexcept;
/** @brief The NVS fake in region @p slot, or nullptr. */
hf::host::MockNvs* Nvs(size_t slot) noexcept;
/** @brief The CAN fake, or nullptr. */
hf::host::MockCan* Can() noexcept;
/** @brief The UART fake on @p port, or nullptr. */
hf::host::MockUart* Uart(uint8_t port) noexcept;
/** @brief Make LedStripBegin fail. */
void FailLedStrip(bool fail = true) noexcept;
/** @brief Last colour shown on LED @p index (after LedStripShow). */
uint32_t LedShownPixel(uint16_t index) noexcept;

// ── System ────────────────────────────────────────────────────────────────
/** @brief Value LastResetReason() returns. */
void SetResetReason(uint32_t reason) noexcept;
/** @brief Number of SystemRestart() calls (the host version throws after counting). */
int RestartCalls() noexcept;

}  // namespace flux::bsp::host

#endif  // FLUX_BSP_HOST_BIND_H_
