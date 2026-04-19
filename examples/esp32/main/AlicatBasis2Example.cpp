/**
 * @file AlicatBasis2Example.cpp
 * @brief Comprehensive Flux V1 example exercising the Alicat BASIS-2 driver
 *        + handler over an ESP32-S3 UART configured for RS-485.
 *
 * @details The example walks through the full bring-up + steady-state loop
 *          a real instrument would go through:
 *
 *            1. Bring up Flux HAL (so the rest of the board is ready).
 *            2. Construct an EspUart in HF_UART_MODE_RS485 half-duplex on a
 *               dedicated port (UART1 on TX=GPIO17/RX=GPIO18 in this demo;
 *               override at the top of the file for your own hardware).
 *            3. Sweep the bus with `Discover()` to enumerate which BASIS-2
 *               addresses are alive (useful at first power-up).
 *            4. For every present address, create an `AlicatBasis2Handler`
 *               sharing the bus mutex; pull `ReadIdentity()` and print
 *               firmware / serial / units / full-scale.
 *            5. Demonstrate the configuration writes the datasheet exposes:
 *                 - `SetGas`, `SetReferenceTemperatureC`, `SetFlowAveragingMs`
 *                 - `SetCommWatchdogMs`, `SetTotalizerLimitMode`
 *                 - `SetSetpointSource`, `SetSetpoint` (controllers)
 *                 - `Tare`, `ResetTotalizer`
 *            6. Enter a periodic `ReadInstantaneous()` loop printing flow,
 *               temperature, totalizer, valve drive, status bitfield.
 *
 *          This is the canonical reference for projects that want to embed
 *          BASIS-2 sensors as ground-truth digital readbacks alongside an
 *          ADS7952 sampling their analog output for the live control loop.
 *
 * @copyright Copyright (c) 2026 HardFOC. All rights reserved.
 */

#include "api/Flux.h"
#include "esp_log.h"

#include "core/hf-core-drivers/internal/hf-internal-interface-wrap/inc/mcu/esp32/EspUart.h"
#include "core/hf-core-drivers/internal/hf-internal-interface-wrap/inc/mcu/esp32/utils/EspTypes_UART.h"
#include "handlers/alicat_basis2/AlicatBasis2Handler.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <array>
#include <cstdio>
#include <cstring>

namespace al = alicat_basis2;

static constexpr const char* TAG = "Basis2Example";

// ─── Bus configuration — adjust for your hardware ────────────────────────────
//
// The BASIS-2 datasheet recommends RS-485 half-duplex when more than one
// instrument shares a bus. ESP-IDF's UART driver handles DE/RE toggling
// automatically when configured in `HF_UART_MODE_RS485`.

namespace cfg {
constexpr int      kUartPort      = 1;
constexpr int      kUartTxGpio    = 17;
constexpr int      kUartRxGpio    = 18;
constexpr int      kUartRtsGpio   = 8;     // DE/RE on RS-485 transceiver, -1 if not used
constexpr uint32_t kInitialBaud   = 38400; // BASIS-2 factory default

// Up to 12 BASIS-2 instruments may share this bus during development.
// Adjust addresses for your bench (factory default is 1 / unit 'A' for all).
constexpr std::uint8_t kMaxMeters = 12;

// Periodic read interval.
constexpr uint32_t kReadIntervalMs = 1000;
}  // namespace cfg

//==============================================================================
// Helpers
//==============================================================================

static const char* SetpointSourceName(int v) {
    switch (v) {
        case 0: return "Analog";
        case 1: return "DigitalSaved";
        case 2: return "DigitalUnsaved";
    }
    return "?";
}

//==============================================================================
// app_main
//==============================================================================

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "Flux HAL Alicat BASIS-2 example");

    // 1) Bring up the Flux HAL (sensors / valves not used by this example,
    //    but Logger and the global Flux singleton come up).
    auto& flux = Flux::GetInstance();
    if (!flux.EnsureInitialized()) {
        ESP_LOGW(TAG, "Flux init failed (this example only needs UART; continuing)");
    }

    // 2) Configure UART1 for RS-485 half-duplex at the factory baud.
    hf_uart_config_t uart_cfg{};
    uart_cfg.port_number  = cfg::kUartPort;
    uart_cfg.baud_rate    = cfg::kInitialBaud;
    uart_cfg.tx_pin       = cfg::kUartTxGpio;
    uart_cfg.rx_pin       = cfg::kUartRxGpio;
    uart_cfg.data_bits    = hf_uart_data_bits_t::HF_UART_DATA_8_BITS;
    uart_cfg.stop_bits    = hf_uart_stop_bits_t::HF_UART_STOP_BITS_1;
    uart_cfg.parity       = hf_uart_parity_t::HF_UART_PARITY_DISABLE;
    uart_cfg.flow_control = hf_uart_flow_ctrl_t::HF_UART_HW_FLOWCTRL_DISABLE;

    static EspUart uart(uart_cfg);
    if (!uart.EnsureInitialized()) {
        ESP_LOGE(TAG, "EspUart::EnsureInitialized failed — aborting");
        return;
    }
    if (uart.SetCommunicationMode(hf_uart_mode_t::HF_UART_MODE_RS485) !=
        hf_uart_err_t::UART_SUCCESS) {
        ESP_LOGW(TAG, "Failed to switch UART to RS-485 mode (continuing in plain UART mode)");
    } else {
        ESP_LOGI(TAG, "UART%d is in RS-485 half-duplex mode (DE/RE on GPIO%d)",
                 cfg::kUartPort, cfg::kUartRtsGpio);
    }

    // 3) Bus discovery sweep with a probe handler bound to address 1.
    static RtosMutex bus_mutex;
    auto probe_cfg = MakeDefaultBasis2Config(/*addr=*/1, /*idx=*/0);
    static AlicatBasis2Handler probe(uart, probe_cfg, &bus_mutex);

    std::array<std::uint8_t, 32> present{};
    const auto disc = probe.Discover(present.data(), present.size(), /*timeout_ms=*/40);
    if (!disc.ok()) {
        ESP_LOGW(TAG, "Bus discovery failed: %s — continuing with default address 1",
                 al::ToString(disc.error).data());
    } else {
        ESP_LOGI(TAG, "Bus discovery: %u BASIS-2 instrument(s) responded", disc.value);
        for (uint16_t a = 1; a <= 247; ++a) {
            if (present[a / 8] & (1U << (a % 8))) {
                ESP_LOGI(TAG, "    addr=%u present", a);
            }
        }
    }

    // 4) Use the probe handler to talk to address 1 (factory default). For
    //    a multi-instrument bench, instantiate one AlicatBasis2Handler per
    //    discovered address — they all share the same `bus_mutex`.
    if (!probe.EnsureInitialized()) {
        ESP_LOGE(TAG, "Failed to read identity at addr=1 — is the instrument powered & wired?");
        // Don't return — continue to demonstrate the loop logic.
    } else {
        const auto& id = probe.Identity();
        ESP_LOGI(TAG, "BASIS-2 @ addr=%u identity:", id.modbus_address);
        ESP_LOGI(TAG, "    firmware     = %u.%u.%u (raw 0x%04X)",
                 id.fw_major, id.fw_minor, id.fw_patch, id.firmware_version_raw);
        ESP_LOGI(TAG, "    serial       = %s",  id.serial_number);
        ESP_LOGI(TAG, "    ascii unit   = '%c'", id.ascii_unit_id);
        ESP_LOGI(TAG, "    flow units   = %s",  al::ToString(id.flow_units).data());
        ESP_LOGI(TAG, "    full-scale   = %lu (in user units / 1000)",
                 static_cast<unsigned long>(id.full_scale_flow_raw));
    }

    // 5) Configuration walkthrough — best-effort, log each result.
    auto log_rc = [](const char* what, al::DriverError e) {
        if (e == al::DriverError::None) {
            ESP_LOGI(TAG, "    %s: ok", what);
        } else {
            ESP_LOGW(TAG, "    %s: %s", what, al::ToString(e).data());
        }
    };

    ESP_LOGI(TAG, "Configuration walkthrough:");
    log_rc("SetGas(N2)",                  probe.SetGas(al::Gas::Nitrogen).error);
    log_rc("SetReferenceTemperatureC(25)", probe.SetReferenceTemperatureC(25.0f).error);
    log_rc("SetFlowAveragingMs(50)",      probe.SetFlowAveragingMs(50).error);
    log_rc("SetTotalizerLimitMode(SaturateNoError)",
           probe.SetTotalizerLimitMode(al::TotalizerLimitMode::SaturateNoError).error);
    log_rc("SetCommWatchdogMs(2000)",     probe.SetCommWatchdogMs(2000).error);

    // Setpoint source + setpoint demo (controllers only — meters will likely
    // return ModbusException for these registers; we report and move on).
    ESP_LOGI(TAG, "Setpoint demo (controllers only):");
    const auto sp_src = al::SetpointSource::DigitalUnsaved;
    log_rc("SetSetpointSource(DigitalUnsaved)",
           probe.SetSetpointSource(sp_src).error);
    ESP_LOGI(TAG, "    setpoint source set to %s", SetpointSourceName(static_cast<int>(sp_src)));
    log_rc("SetSetpoint(2.5)",            probe.SetSetpoint(2.5f).error);

    // Tare + totalizer reset
    log_rc("Tare()",                      probe.Tare().error);
    log_rc("ResetTotalizer()",            probe.ResetTotalizer().error);

    // 6) Steady-state read loop — every kReadIntervalMs print live data.
    ESP_LOGI(TAG, "Entering periodic read loop @ %lu ms",
             static_cast<unsigned long>(cfg::kReadIntervalMs));
    while (true) {
        const auto live = probe.ReadInstantaneous();
        if (live.ok()) {
            const auto& d = live.value;
            ESP_LOGI(TAG,
                     "BASIS-2 %s | T=%.2f C | flow=%.4f | total=%.4f | sp=%.4f | drive=%.1f%% | "
                     "[MOV=%d TOV=%d OVR=%d HLD=%d VTM=%d]",
                     al::GasShortName(d.gas).data(),
                     static_cast<double>(d.temperature_c),
                     static_cast<double>(d.flow),
                     static_cast<double>(d.total_volume),
                     static_cast<double>(d.setpoint),
                     static_cast<double>(d.valve_drive_pct),
                     d.mass_flow_overrange, d.temperature_overrange,
                     d.totalizer_overrange, d.valve_held,
                     d.valve_thermal_management);
        } else {
            ESP_LOGW(TAG, "ReadInstantaneous failed: %s",
                     al::ToString(live.error).data());
        }
        vTaskDelay(pdMS_TO_TICKS(cfg::kReadIntervalMs));
    }
}
