/**
 * @file FluxPlatformBindEsp32s3.cpp
 * @brief ESP32-S3 implementation of the Flux platform binding (`bsp_bind/FluxPlatformBind.h`).
 *
 * @details The only translation unit of the HAL that names `Esp*` wrappers, ESP-IDF headers or the
 *          WS2812 RMT driver. Every object lives in a fixed `std::optional` slot in static storage, so
 *          the binding itself never calls the heap allocator; the ESP-IDF drivers underneath (SPI
 *          master, TWAI, UART, RMT) allocate their own queues inside the `Create*` call, which runs
 *          from the owning manager's `EnsureInitialized()`.
 *
 * @author HardFOC Team
 * @date 2026
 */

#include "bsp_bind/FluxPlatformBind.h"

#include <array>
#include <optional>

#include "core/hf-core-drivers/internal/hf-internal-interface-wrap/inc/mcu/esp32/EspAdc.h"
#include "core/hf-core-drivers/internal/hf-internal-interface-wrap/inc/mcu/esp32/EspCan.h"
#include "core/hf-core-drivers/internal/hf-internal-interface-wrap/inc/mcu/esp32/EspGpio.h"
#include "core/hf-core-drivers/internal/hf-internal-interface-wrap/inc/mcu/esp32/EspNvs.h"
#include "core/hf-core-drivers/internal/hf-internal-interface-wrap/inc/mcu/esp32/EspSpi.h"
#include "core/hf-core-drivers/internal/hf-internal-interface-wrap/inc/mcu/esp32/EspTemperature.h"
#include "core/hf-core-drivers/internal/hf-internal-interface-wrap/inc/mcu/esp32/EspUart.h"
#ifdef HARDFOC_USB_SERIAL_JTAG_SUPPORT
#include "core/hf-core-drivers/internal/hf-internal-interface-wrap/inc/mcu/esp32/EspUsbSerialJtag.h"
#endif
#ifdef HARDFOC_WS2812_SUPPORT
#include "core/hf-core-drivers/external/hf-ws2812-rmt-driver/inc/ws2812_cpp.hpp"
#endif

#include "esp_heap_caps.h"
#include "esp_system.h"

namespace flux::bsp {

namespace {

// ── Static storage: one slot per object the managers can ask for ─────────────
std::array<std::optional<EspGpio>, kMaxGpioSlots> g_gpio;
std::optional<EspAdc>                              g_adc;
std::optional<EspTemperature>                      g_die_temp;
std::array<std::optional<EspNvs>, kMaxNvsRegions>  g_nvs;
std::array<std::optional<EspSpiBus>, kMaxSpiBuses> g_spi;
std::array<uint8_t, kMaxSpiBuses>                  g_spi_device_count{};
std::optional<EspCan>                              g_can;
std::array<std::optional<EspUart>, kMaxUartPorts>  g_uart;
#ifdef HARDFOC_WS2812_SUPPORT
std::optional<WS2812Strip>                         g_led;
#endif

/* WS2812 bit timing (RMT ticks) and initial brightness used on Flux V1 since bring-up. */
constexpr uint16_t kLedT0h = 14;
constexpr uint16_t kLedT1h = 52;
constexpr uint16_t kLedT0l = 52;
constexpr uint16_t kLedT1l = 52;
constexpr uint8_t  kLedInitialBrightness = 255;

}  // namespace

//==============================================================================
// GPIO
//==============================================================================

BaseGpio* CreateGpio(size_t slot, const GpioSpec& spec) noexcept {
    if (slot >= g_gpio.size() || g_gpio[slot].has_value()) {
        return nullptr;
    }
    g_gpio[slot].emplace(spec.pin, spec.direction, spec.active_state, spec.output_mode, spec.pull_mode);
    return &*g_gpio[slot];
}

void DestroyGpio(size_t slot) noexcept {
    if (slot < g_gpio.size()) {
        g_gpio[slot].reset();
    }
}

//==============================================================================
// MCU ADC AND DIE TEMPERATURE
//==============================================================================

uint8_t InternalAdcChannelCount() noexcept {
    return static_cast<uint8_t>(HF_ESP32_ADC_MAX_CHANNELS);
}

BaseAdc* CreateInternalAdc(uint8_t unit, uint32_t channel_mask) noexcept {
    if (g_adc.has_value()) {
        return &*g_adc;
    }
    // EspAdc validation: channel_configs[i].channel_id == i and at least one channel enabled.
    hf_adc_unit_config_t cfg{};
    cfg.unit_id = unit;
    for (uint8_t ch = 0; ch < HF_ESP32_ADC_MAX_CHANNELS; ++ch) {
        if ((channel_mask & (1UL << ch)) == 0U) {
            continue;
        }
        cfg.channel_configs[ch].channel_id = ch;
        cfg.channel_configs[ch].attenuation = hf_adc_atten_t::ATTEN_DB_12;
        cfg.channel_configs[ch].bitwidth = hf_adc_bitwidth_t::WIDTH_12BIT;
        cfg.channel_configs[ch].enabled = true;
    }
    g_adc.emplace(cfg);
    if (!g_adc->EnsureInitialized()) {
        g_adc.reset();
        return nullptr;
    }
    for (uint8_t ch = 0; ch < HF_ESP32_ADC_MAX_CHANNELS; ++ch) {
        if ((channel_mask & (1UL << ch)) != 0U) {
            (void)g_adc->ConfigureChannel(ch, hf_adc_atten_t::ATTEN_DB_12);
        }
    }
    return &*g_adc;
}

void DestroyInternalAdc() noexcept {
    g_adc.reset();
}

BaseTemperature* CreateDieTemperature() noexcept {
    if (!g_die_temp.has_value()) {
        g_die_temp.emplace();
    }
    return &*g_die_temp;
}

void DestroyDieTemperature() noexcept {
    g_die_temp.reset();
}

//==============================================================================
// NVS
//==============================================================================

BaseNvs& NvsRegion(size_t slot, const char* name_space) noexcept {
    // Out-of-range slots alias the last region; NvsManager only asks for 0..kMaxNvsRegions-1.
    const size_t i = (slot < g_nvs.size()) ? slot : g_nvs.size() - 1;
    if (!g_nvs[i].has_value()) {
        g_nvs[i].emplace(name_space);
    }
    return *g_nvs[i];
}

//==============================================================================
// SPI
//==============================================================================

bool CreateSpiBus(size_t bus, const SpiBusSpec& spec) noexcept {
    if (bus >= g_spi.size()) {
        return false;
    }
    if (g_spi[bus].has_value()) {
        return true;
    }
    hf_spi_bus_config_t cfg{};
    cfg.host = static_cast<hf_host_id_t>(spec.controller);
    cfg.miso_pin = spec.miso;
    cfg.mosi_pin = spec.mosi;
    cfg.sclk_pin = spec.sclk;
    cfg.dma_channel = 0;  // auto-select
    cfg.timeout_ms = spec.timeout_ms;
    cfg.use_iomux = spec.use_iomux;
    g_spi[bus].emplace(cfg);
    if (!g_spi[bus]->Initialize()) {
        g_spi[bus].reset();
        return false;
    }
    g_spi_device_count[bus] = 0;
    return true;
}

BaseSpi* CreateSpiDevice(size_t bus, const SpiDeviceSpec& spec) noexcept {
    if (bus >= g_spi.size() || !g_spi[bus].has_value() || g_spi_device_count[bus] >= kMaxSpiDevicesPerBus) {
        return nullptr;
    }
    hf_spi_device_config_t dev{};
    dev.cs_pin = spec.cs;
    dev.clock_speed_hz = spec.clock_hz;
    dev.mode = static_cast<hf_spi_mode_t>(spec.mode & 0x3U);
    const int idx = g_spi[bus]->CreateDevice(dev);
    if (idx < 0) {
        return nullptr;
    }
    ++g_spi_device_count[bus];
    return g_spi[bus]->GetDevice(idx);
}

void DestroySpiBus(size_t bus) noexcept {
    if (bus < g_spi.size()) {
        g_spi[bus].reset();
        g_spi_device_count[bus] = 0;
    }
}

//==============================================================================
// CAN
//==============================================================================

BaseCan* CreateCan(const CanSpec& spec) noexcept {
    if (g_can.has_value()) {
        return &*g_can;
    }
    hf_esp_can_config_t cfg{};
    cfg.tx_pin = spec.tx;
    cfg.rx_pin = spec.rx;
    cfg.baud_rate = spec.bitrate;
    g_can.emplace(cfg);
    return &*g_can;
}

void DestroyCan() noexcept {
    g_can.reset();
}

//==============================================================================
// UART AND USB SERIAL/JTAG
//==============================================================================

BaseUart* CreateUart(const UartSpec& spec, bool* rs485) noexcept {
    if (rs485 != nullptr) {
        *rs485 = false;
    }
    if (spec.port >= g_uart.size()) {
        return nullptr;
    }
    if (g_uart[spec.port].has_value()) {
        return &*g_uart[spec.port];
    }
    hf_uart_config_t cfg{};
    cfg.port_number = static_cast<hf_port_num_t>(spec.port);
    cfg.baud_rate = spec.baud;
    cfg.tx_pin = spec.tx;
    cfg.rx_pin = spec.rx;
    cfg.rts_pin = spec.rts;
    cfg.cts_pin = spec.cts;
    cfg.data_bits = hf_uart_data_bits_t::HF_UART_DATA_8_BITS;
    cfg.parity = hf_uart_parity_t::HF_UART_PARITY_DISABLE;
    cfg.stop_bits = hf_uart_stop_bits_t::HF_UART_STOP_BITS_1;
    cfg.flow_control = hf_uart_flow_ctrl_t::HF_UART_HW_FLOWCTRL_DISABLE;
    cfg.tx_buffer_size = spec.tx_buffer_size;
    cfg.rx_buffer_size = spec.rx_buffer_size;
    auto& u = g_uart[spec.port].emplace(cfg);
    if (!u.EnsureInitialized()) {
        g_uart[spec.port].reset();
        return nullptr;
    }
    if (spec.rts >= 0 && rs485 != nullptr) {
        *rs485 = (u.SetCommunicationMode(hf_uart_mode_t::HF_UART_MODE_RS485) == hf_uart_err_t::UART_SUCCESS);
    }
    return &u;
}

void DestroyUart(uint8_t port) noexcept {
    if (port < g_uart.size()) {
        g_uart[port].reset();
    }
}

BaseUsbSerialJtag* AcquireUsbSerialJtag(const hf_usb_serial_jtag_config_t& cfg) noexcept {
#ifdef HARDFOC_USB_SERIAL_JTAG_SUPPORT
    // The controller is one silicon block; EspUsbSerialJtag::Default() owns the only instance.
    auto& inst = EspUsbSerialJtag::Default(cfg);
    return inst.EnsureInitialized() ? &inst : nullptr;
#else
    (void)cfg;
    return nullptr;
#endif
}

//==============================================================================
// STATUS LED STRIP
//==============================================================================

#ifdef HARDFOC_WS2812_SUPPORT
bool LedStripBegin(const LedStripSpec& spec) noexcept {
    if (g_led.has_value()) {
        return true;
    }
    g_led.emplace(static_cast<gpio_num_t>(spec.pin), static_cast<int>(spec.channel),
                  static_cast<uint32_t>(spec.count), LedType::RGB, kLedT0h, kLedT1h, kLedT0l, kLedT1l,
                  kLedInitialBrightness);
    if (g_led->Begin() != ESP_OK) {
        g_led.reset();
        return false;
    }
    return true;
}

void LedStripSetPixel(uint16_t index, uint32_t rgb) noexcept {
    if (g_led.has_value()) {
        g_led->SetPixel(index, rgb);
    }
}

void LedStripSetBrightness(uint8_t brightness) noexcept {
    if (g_led.has_value()) {
        g_led->SetBrightness(brightness);
    }
}

bool LedStripShow() noexcept {
    return g_led.has_value() && g_led->Show() == ESP_OK;
}

void LedStripEnd() noexcept {
    g_led.reset();
}
#else
bool LedStripBegin(const LedStripSpec& spec) noexcept {
    (void)spec;
    return false;
}
void LedStripSetPixel(uint16_t index, uint32_t rgb) noexcept {
    (void)index;
    (void)rgb;
}
void LedStripSetBrightness(uint8_t brightness) noexcept {
    (void)brightness;
}
bool LedStripShow() noexcept {
    return false;
}
void LedStripEnd() noexcept {}
#endif

//==============================================================================
// SYSTEM
//==============================================================================

uint32_t LastResetReason() noexcept {
    return static_cast<uint32_t>(esp_reset_reason());
}

void SystemRestart() noexcept {
    esp_restart();
    while (true) {
        // esp_restart() does not return.
    }
}

uint32_t FreeHeapBytes() noexcept {
    return static_cast<uint32_t>(heap_caps_get_free_size(MALLOC_CAP_DEFAULT));
}

uint32_t LargestFreeHeapBlock() noexcept {
    return static_cast<uint32_t>(heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));
}

}  // namespace flux::bsp
