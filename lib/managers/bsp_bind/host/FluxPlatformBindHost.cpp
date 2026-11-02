/**
 * @file FluxPlatformBindHost.cpp
 * @brief Host implementation of the Flux platform binding over the hf-core host fakes.
 *
 * @details Same slot layout as the ESP32-S3 binding (`bsp_bind/esp32s3/`), so a manager under test
 *          runs exactly the code it runs on the board; only the objects behind the `Base*` pointers
 *          differ. Scripting and inspection: `FluxHostBind.h`.
 *
 * @author HardFOC Team
 * @date 2026
 */

#include "bsp_bind/host/FluxHostBind.h"

#include <array>
#include <cstdlib>
#include <map>
#include <optional>
#include <utility>

namespace flux::bsp {

namespace {

struct SpiDeviceSlot {
    std::optional<hf::host::MockSpi> spi;
    SpiDeviceSpec spec{};
};

struct SpiBusSlot {
    bool up{false};
    SpiBusSpec spec{};
    std::array<SpiDeviceSlot, kMaxSpiDevicesPerBus> devices{};
    size_t device_count{0};
};

struct HostState {
    std::array<std::optional<hf::host::MockGpio>, kMaxGpioSlots> gpio{};
    std::array<GpioSpec, kMaxGpioSlots> gpio_spec{};
    std::map<hf_pin_num_t, bool> gpio_fail_init{};

    std::optional<hf::host::MockAdc> adc{};
    uint32_t adc_mask{0};
    bool adc_fail{false};

    std::optional<hf::host::MockTemperature> die_temp{};
    bool die_temp_fail{false};
    std::array<std::optional<hf::host::MockNvs>, kMaxNvsRegions> nvs{};

    std::array<SpiBusSlot, kMaxSpiBuses> spi{};
    std::array<bool, kMaxSpiBuses> spi_fail{};
    std::map<hf_pin_num_t, hf::host::MockSpi::Responder> spi_responders{};

    std::optional<hf::host::MockCan> can{};
    std::array<std::optional<hf::host::MockUart>, kMaxUartPorts> uart{};

    bool led_up{false};
    bool led_fail{false};
    std::array<uint32_t, 8> led_staged{};
    std::array<uint32_t, 8> led_shown{};

    uint32_t reset_reason{0};
    int restart_calls{0};
};

void ClearBus(SpiBusSlot& b) noexcept {
    b.up = false;
    b.spec = SpiBusSpec{};
    for (auto& d : b.devices) {
        d.spi.reset();
        d.spec = SpiDeviceSpec{};
    }
    b.device_count = 0;
}

HostState& S() noexcept {
    static HostState state;
    return state;
}

}  // namespace

//==============================================================================
// GPIO
//==============================================================================

BaseGpio* CreateGpio(size_t slot, const GpioSpec& spec) noexcept {
    auto& s = S();
    if (slot >= s.gpio.size() || s.gpio[slot].has_value()) {
        return nullptr;
    }
    auto& g = s.gpio[slot].emplace(spec.pin, spec.direction, spec.active_state);
    s.gpio_spec[slot] = spec;
    const auto it = s.gpio_fail_init.find(spec.pin);
    if (it != s.gpio_fail_init.end() && it->second) {
        g.SetInitializeResult(false);
    }
    return &g;
}

void DestroyGpio(size_t slot) noexcept {
    if (slot < S().gpio.size()) {
        S().gpio[slot].reset();
    }
}

//==============================================================================
// MCU ADC AND DIE TEMPERATURE
//==============================================================================

uint8_t InternalAdcChannelCount() noexcept {
    return 10;  // ESP32-S3 ADC1
}

BaseAdc* CreateInternalAdc(uint8_t unit, uint32_t channel_mask) noexcept {
    (void)unit;
    auto& s = S();
    if (s.adc_fail) {
        return nullptr;
    }
    if (!s.adc.has_value()) {
        s.adc.emplace(InternalAdcChannelCount());
        s.adc_mask = channel_mask;
        (void)s.adc->EnsureInitialized();
    }
    return &*s.adc;
}

void DestroyInternalAdc() noexcept {
    S().adc.reset();
    S().adc_mask = 0;
}

BaseTemperature* CreateDieTemperature() noexcept {
    if (!S().die_temp.has_value()) {
        S().die_temp.emplace();
        S().die_temp->SetInitializeResult(!S().die_temp_fail);
    }
    return &*S().die_temp;
}

void DestroyDieTemperature() noexcept {
    S().die_temp.reset();
}

//==============================================================================
// NVS
//==============================================================================

BaseNvs& NvsRegion(size_t slot, const char* name_space) noexcept {
    auto& s = S();
    const size_t i = (slot < s.nvs.size()) ? slot : s.nvs.size() - 1;
    if (!s.nvs[i].has_value()) {
        s.nvs[i].emplace(name_space);
    }
    return *s.nvs[i];
}

//==============================================================================
// SPI
//==============================================================================

bool CreateSpiBus(size_t bus, const SpiBusSpec& spec) noexcept {
    auto& s = S();
    if (bus >= s.spi.size() || s.spi_fail[bus]) {
        return false;
    }
    if (!s.spi[bus].up) {
        ClearBus(s.spi[bus]);
        s.spi[bus].up = true;
        s.spi[bus].spec = spec;
    }
    return true;
}

BaseSpi* CreateSpiDevice(size_t bus, const SpiDeviceSpec& spec) noexcept {
    auto& s = S();
    if (bus >= s.spi.size() || !s.spi[bus].up || s.spi[bus].device_count >= kMaxSpiDevicesPerBus) {
        return nullptr;
    }
    auto& slot = s.spi[bus].devices[s.spi[bus].device_count++];
    auto& dev = slot.spi.emplace();
    slot.spec = spec;
    const auto it = s.spi_responders.find(spec.cs);
    if (it != s.spi_responders.end()) {
        dev.SetResponder(it->second);
    }
    return &dev;
}

void DestroySpiBus(size_t bus) noexcept {
    if (bus < S().spi.size()) {
        ClearBus(S().spi[bus]);
    }
}

//==============================================================================
// CAN, UART, USB SERIAL/JTAG
//==============================================================================

BaseCan* CreateCan(const CanSpec& spec) noexcept {
    (void)spec;
    if (!S().can.has_value()) {
        S().can.emplace();
    }
    return &*S().can;
}

void DestroyCan() noexcept {
    S().can.reset();
}

BaseUart* CreateUart(const UartSpec& spec, bool* rs485) noexcept {
    if (rs485 != nullptr) {
        *rs485 = spec.rts >= 0;
    }
    auto& s = S();
    if (spec.port >= s.uart.size()) {
        return nullptr;
    }
    if (!s.uart[spec.port].has_value()) {
        s.uart[spec.port].emplace(static_cast<hf_port_num_t>(spec.port));
        (void)s.uart[spec.port]->EnsureInitialized();
    }
    return &*s.uart[spec.port];
}

void DestroyUart(uint8_t port) noexcept {
    if (port < S().uart.size()) {
        S().uart[port].reset();
    }
}

BaseUsbSerialJtag* AcquireUsbSerialJtag(const hf_usb_serial_jtag_config_t& cfg) noexcept {
    (void)cfg;
    return nullptr;
}

//==============================================================================
// STATUS LED STRIP
//==============================================================================

bool LedStripBegin(const LedStripSpec& spec) noexcept {
    (void)spec;
    auto& s = S();
    if (s.led_fail) {
        return false;
    }
    s.led_up = true;
    return true;
}

void LedStripSetPixel(uint16_t index, uint32_t rgb) noexcept {
    if (index < S().led_staged.size()) {
        S().led_staged[index] = rgb;
    }
}

void LedStripSetBrightness(uint8_t brightness) noexcept {
    (void)brightness;
}

bool LedStripShow() noexcept {
    auto& s = S();
    if (!s.led_up) {
        return false;
    }
    s.led_shown = s.led_staged;
    return true;
}

void LedStripEnd() noexcept {
    S().led_up = false;
}

//==============================================================================
// SYSTEM
//==============================================================================

uint32_t LastResetReason() noexcept {
    return S().reset_reason;
}

void SystemRestart() noexcept {
    ++S().restart_calls;
    std::abort();
}

uint32_t FreeHeapBytes() noexcept {
    return 0;
}

uint32_t LargestFreeHeapBlock() noexcept {
    return 0;
}

//==============================================================================
// HOST SCRIPTING / INSPECTION
//==============================================================================

namespace host {

void Reset() noexcept {
    auto& s = S();
    for (auto& g : s.gpio) {
        g.reset();
    }
    s.gpio_fail_init.clear();
    s.adc.reset();
    s.adc_mask = 0;
    s.adc_fail = false;
    s.die_temp.reset();
    s.die_temp_fail = false;
    // NVS regions are referenced by NvsManager for the life of the program: keep the
    // objects where they are and clear them by re-creating them in place. BaseNvs keeps
    // the namespace pointer, which is the manager's string literal, so it stays valid.
    for (auto& n : s.nvs) {
        if (n.has_value()) {
            const char* ns = n->GetNamespace();
            n.reset();
            n.emplace(ns);
        }
    }
    for (auto& b : s.spi) {
        ClearBus(b);
    }
    s.spi_fail.fill(false);
    s.spi_responders.clear();
    s.can.reset();
    for (auto& u : s.uart) {
        u.reset();
    }
    s.led_up = false;
    s.led_fail = false;
    s.led_staged.fill(0);
    s.led_shown.fill(0);
    s.reset_reason = 0;
    s.restart_calls = 0;
}

hf::host::MockGpio* Gpio(size_t slot) noexcept {
    auto& s = S();
    return (slot < s.gpio.size() && s.gpio[slot].has_value()) ? &*s.gpio[slot] : nullptr;
}

hf::host::MockGpio* GpioByPin(hf_pin_num_t pin) noexcept {
    auto& s = S();
    for (size_t i = 0; i < s.gpio.size(); ++i) {
        if (s.gpio[i].has_value() && s.gpio_spec[i].pin == pin) {
            return &*s.gpio[i];
        }
    }
    return nullptr;
}

const GpioSpec* GpioSpecOf(size_t slot) noexcept {
    auto& s = S();
    return (slot < s.gpio.size() && s.gpio[slot].has_value()) ? &s.gpio_spec[slot] : nullptr;
}

void FailGpioInit(hf_pin_num_t pin) noexcept {
    S().gpio_fail_init[pin] = true;
}

void SetSpiResponder(hf_pin_num_t cs, hf::host::MockSpi::Responder responder) {
    auto& s = S();
    s.spi_responders[cs] = responder;
    if (auto* dev = SpiDeviceByCs(cs)) {
        dev->SetResponder(std::move(responder));
    }
}

hf::host::MockSpi* SpiDeviceByCs(hf_pin_num_t cs) noexcept {
    for (auto& b : S().spi) {
        for (size_t i = 0; i < b.device_count; ++i) {
            if (b.devices[i].spi.has_value() && b.devices[i].spec.cs == cs) {
                return &*b.devices[i].spi;
            }
        }
    }
    return nullptr;
}

const SpiDeviceSpec* SpiDeviceSpecOf(hf_pin_num_t cs) noexcept {
    for (auto& b : S().spi) {
        for (size_t i = 0; i < b.device_count; ++i) {
            if (b.devices[i].spi.has_value() && b.devices[i].spec.cs == cs) {
                return &b.devices[i].spec;
            }
        }
    }
    return nullptr;
}

const SpiBusSpec* SpiBusSpecOf(size_t bus) noexcept {
    auto& s = S();
    return (bus < s.spi.size() && s.spi[bus].up) ? &s.spi[bus].spec : nullptr;
}

void FailSpiBus(size_t bus, bool fail) noexcept {
    if (bus < S().spi_fail.size()) {
        S().spi_fail[bus] = fail;
    }
}

hf::host::MockAdc* InternalAdc() noexcept {
    return S().adc.has_value() ? &*S().adc : nullptr;
}

uint32_t InternalAdcChannelMask() noexcept {
    return S().adc_mask;
}

void FailInternalAdc(bool fail) noexcept {
    S().adc_fail = fail;
}

hf::host::MockTemperature* DieTemperature() noexcept {
    return S().die_temp.has_value() ? &*S().die_temp : nullptr;
}

void FailDieTemperatureInit(bool fail) noexcept {
    S().die_temp_fail = fail;
}

hf::host::MockNvs* Nvs(size_t slot) noexcept {
    auto& s = S();
    return (slot < s.nvs.size() && s.nvs[slot].has_value()) ? &*s.nvs[slot] : nullptr;
}

hf::host::MockCan* Can() noexcept {
    return S().can.has_value() ? &*S().can : nullptr;
}

hf::host::MockUart* Uart(uint8_t port) noexcept {
    auto& s = S();
    return (port < s.uart.size() && s.uart[port].has_value()) ? &*s.uart[port] : nullptr;
}

void FailLedStrip(bool fail) noexcept {
    S().led_fail = fail;
}

uint32_t LedShownPixel(uint16_t index) noexcept {
    return index < S().led_shown.size() ? S().led_shown[index] : 0;
}

void SetResetReason(uint32_t reason) noexcept {
    S().reset_reason = reason;
}

int RestartCalls() noexcept {
    return S().restart_calls;
}

}  // namespace host

}  // namespace flux::bsp
