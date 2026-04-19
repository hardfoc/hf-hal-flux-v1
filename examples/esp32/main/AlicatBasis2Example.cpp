/**
 * @file AlicatBasis2Example.cpp
 * @brief Comprehensive Flux V1 example exercising the **Alicat BASIS-2**
 *        driver and `AlicatBasis2Handler` over an ESP32-S3 UART
 *        configured for RS-485 half-duplex.
 *
 * @details Walks through every public handler entry point so you can use
 *          this file as a working "cookbook" for embedding BASIS-2 mass
 *          flow meters / controllers in a Flux-based project:
 *
 *            1. Bring up the Flux HAL (so loggers / Flux::GetInstance()
 *               are alive even though this example only needs UART).
 *            2. Construct a single `EspUart` on **UART1** with TX=GPIO17
 *               and RX=GPIO18 (override at the top of the file). The
 *               UART is then switched to `HF_UART_MODE_RS485` so ESP-IDF
 *               drives DE/RE automatically through the RTS pin.
 *            3. **Multi-baud discovery + normalisation**:
 *                 - Sweep every supported BASIS-2 baud rate
 *                   (4800/9600/19200/38400/57600/115200) using
 *                   `DiscoverAcrossBauds()` so devices that were
 *                   previously reconfigured aren't invisible.
 *                 - Run `NormalizeBusBaud(target)` to bring every
 *                   discovered device onto a single baud (`38400` by
 *                   default — flip `cfg::kNormalizeBus` off to skip).
 *                 - Re-run a single-baud `Discover()` at the target
 *                   baud as a sanity check.
 *            4. For every present address build a dedicated
 *               `AlicatBasis2Handler` that **shares the same bus mutex**;
 *               read identity (firmware/serial/units/full-scale).
 *            5. **Configuration walkthrough** exercising every public
 *               write entry point: gas, reference temperature,
 *               averaging, totalizer mode, comm watchdog, autotare,
 *               max-setpoint ramp, totalizer batch, setpoint source,
 *               setpoint, tare, totalizer reset.
 *            6. Quick **measurement-window demo** — configure trigger,
 *               start a 200-sample window, read back min/max/avg.
 *            7. Steady-state **periodic poll loop**: every iteration
 *               prints `ReadInstantaneous()` for every present
 *               instrument and once per minute re-reads identity to
 *               confirm the link is healthy.
 *
 *          This file is the canonical reference for projects that want
 *          to embed BASIS-2 sensors as ground-truth digital readbacks
 *          alongside an ADS7952 sampling their analog output for the
 *          live control loop.
 *
 * @note  The example is **defensive**: every Modbus call's error code
 *        is logged but the loop continues, so a meter (rather than a
 *        controller) — which legitimately rejects setpoint writes with
 *        `ModbusException` — does not abort the test.
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
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

namespace al = alicat_basis2;

static constexpr const char* TAG = "Basis2Example";

// ─── Bus configuration — adjust for your hardware ────────────────────────────
//
// The BASIS-2 datasheet recommends RS-485 half-duplex when more than one
// instrument shares a bus. ESP-IDF's UART driver toggles DE/RE
// automatically when configured in `HF_UART_MODE_RS485`.

namespace cfg {
constexpr int      kUartPort      = 1;     ///< ESP32-S3 UART1
constexpr int      kUartTxGpio    = 17;    ///< RS-485 driver A (TX)
constexpr int      kUartRxGpio    = 18;    ///< RS-485 driver B (RX)
constexpr int      kUartRtsGpio   = 8;     ///< DE/RE on the transceiver (-1 to leave RTS unused)
constexpr uint32_t kInitialBaud   = 38400; ///< BASIS-2 factory default

/// Cap how many `AlicatBasis2Handler`s the example will create even if
/// discovery finds more devices.
constexpr std::uint8_t kMaxMeters = 12;

/// Cadence of the steady-state ReadInstantaneous() loop.
constexpr uint32_t kReadIntervalMs = 1000;

/// Re-confirm identity once every N polling iterations to make sure the
/// link is still healthy in long-running tests.
constexpr uint32_t kIdentityHeartbeatN = 60;

/// When `true` the example runs a multi-baud discovery sweep at start-up
/// and then `NormalizeBusBaud()` so every device ends up at
/// `kNormalizedBaud` before the steady-state loop starts.
///
/// Set to `false` to skip the sweep entirely (saves ~3 s on boot when
/// you already know everyone is at the factory baud).
constexpr bool     kNormalizeBus      = true;
constexpr uint32_t kNormalizedBaud    = 38400;  // factory default; safe choice
}  // namespace cfg

//==============================================================================
// Helpers
//==============================================================================

namespace {

const char* SetpointSourceName(al::SetpointSource v) {
    switch (v) {
        case al::SetpointSource::Analog:         return "Analog";
        case al::SetpointSource::DigitalSaved:   return "DigitalSaved";
        case al::SetpointSource::DigitalUnsaved: return "DigitalUnsaved";
    }
    return "?";
}

void LogResult(const char* what, al::DriverError e) {
    if (e == al::DriverError::None) {
        ESP_LOGI(TAG, "    %-44s ok", what);
    } else {
        ESP_LOGW(TAG, "    %-44s %s", what, al::ToString(e).data());
    }
}

void PrintIdentity(const al::InstrumentIdentity& id) {
    ESP_LOGI(TAG, "  identity:");
    ESP_LOGI(TAG, "    firmware     = %u.%u.%u (raw 0x%04X)",
             id.fw_major, id.fw_minor, id.fw_patch, id.firmware_version_raw);
    ESP_LOGI(TAG, "    serial       = %s",  id.serial_number);
    ESP_LOGI(TAG, "    addr / unit  = %u / '%c'",
             id.modbus_address, id.ascii_unit_id);
    ESP_LOGI(TAG, "    flow units   = %s",  al::ToString(id.flow_units).data());
    ESP_LOGI(TAG, "    full-scale   = %" PRIu32 " (in user units / 1000)",
             id.full_scale_flow_raw);
}

void PrintInstantaneous(const AlicatBasis2Handler& h,
                        const al::InstantaneousData& d) {
    ESP_LOGI(TAG,
             "  [addr=%u] %s | T=%6.2fC | flow=%9.4f | total=%10.4f | "
             "sp=%7.4f | drive=%5.1f%% | [MOV=%d TOV=%d OVR=%d HLD=%d VTM=%d]",
             h.Address(),
             al::GasShortName(d.gas).data(),
             static_cast<double>(d.temperature_c),
             static_cast<double>(d.flow),
             static_cast<double>(d.total_volume),
             static_cast<double>(d.setpoint),
             static_cast<double>(d.valve_drive_pct),
             d.mass_flow_overrange, d.temperature_overrange,
             d.totalizer_overrange, d.valve_held,
             d.valve_thermal_management);
}

bool BringUpUart(EspUart& uart) {
    if (!uart.EnsureInitialized()) {
        ESP_LOGE(TAG, "EspUart::EnsureInitialized failed — aborting");
        return false;
    }
    if (uart.SetCommunicationMode(hf_uart_mode_t::HF_UART_MODE_RS485) !=
        hf_uart_err_t::UART_SUCCESS) {
        ESP_LOGW(TAG, "Failed to switch UART to RS-485 mode "
                      "(continuing in plain UART mode)");
    } else {
        ESP_LOGI(TAG, "UART%d in RS-485 half-duplex (TX=%d RX=%d DE/RE=%d @ %u bps)",
                 cfg::kUartPort, cfg::kUartTxGpio, cfg::kUartRxGpio,
                 cfg::kUartRtsGpio, static_cast<unsigned>(cfg::kInitialBaud));
    }
    return true;
}

/// Walk every public configuration entry point on the handler.
///
/// Errors are logged but never abort the test — meter (non-controller)
/// instruments will legitimately reject some writes with
/// `ModbusException`.
void ConfigurationWalkthrough(AlicatBasis2Handler& h) {
    ESP_LOGI(TAG, "configuration walkthrough on addr=%u:", h.Address());

    LogResult("SetGas(N2)",
              h.SetGas(al::Gas::Nitrogen).error);
    LogResult("SetReferenceTemperatureC(25)",
              h.SetReferenceTemperatureC(25.0f).error);
    LogResult("SetFlowAveragingMs(50)",
              h.SetFlowAveragingMs(50).error);
    LogResult("SetTotalizerLimitMode(SaturateNoError)",
              h.SetTotalizerLimitMode(al::TotalizerLimitMode::SaturateNoError).error);
    LogResult("SetCommWatchdogMs(2000)",
              h.SetCommWatchdogMs(2000).error);
    LogResult("SetAutotareEnabled(true)",
              h.SetAutotareEnabled(true).error);
    LogResult("SetMaxSetpointRamp(50%/s)",
              h.SetMaxSetpointRamp(50UL * 100UL).error);   // 50 %/s ≈ 50e-2 %/ms × 10^7 → 5e6
    LogResult("SetTotalizerBatch(0=disabled)",
              h.SetTotalizerBatch(0).error);

    ESP_LOGI(TAG, "  setpoint demo (controllers only):");
    constexpr auto kSrc = al::SetpointSource::DigitalUnsaved;
    LogResult("SetSetpointSource(DigitalUnsaved)", h.SetSetpointSource(kSrc).error);
    ESP_LOGI(TAG, "    source name = %s", SetpointSourceName(kSrc));
    LogResult("SetSetpoint(0.0)", h.SetSetpoint(0.0f).error);
    LogResult("SetSetpoint(2.5)", h.SetSetpoint(2.5f).error);

    LogResult("Tare()", h.Tare().error);
    LogResult("ResetTotalizer()", h.ResetTotalizer().error);
}

/// One-shot 200-sample measurement-window demo.
void MeasurementWindowDemo(AlicatBasis2Handler& h) {
    ESP_LOGI(TAG, "measurement-window demo on addr=%u:", h.Address());
    LogResult("ConfigureMeasurementTrigger(0)",
              h.ConfigureMeasurementTrigger(0).error);
    LogResult("StartMeasurementSamples(200)",
              h.StartMeasurementSamples(200).error);

    // 200 samples * ~2.5 ms ≈ 0.5 s — wait a touch longer.
    vTaskDelay(pdMS_TO_TICKS(800));

    auto rc = h.ReadMeasurement();
    if (!rc.ok()) {
        ESP_LOGW(TAG, "    ReadMeasurement(): %s", al::ToString(rc.error).data());
        return;
    }
    const auto& m = rc.value;
    ESP_LOGI(TAG,
             "    samples=%u  T(min/avg/max)=%.2f/%.2f/%.2f  flow_raw(min/avg/max)=%d/%d/%d",
             m.num_samples_collected,
             static_cast<double>(m.min_temperature_c),
             static_cast<double>(m.avg_temperature_c),
             static_cast<double>(m.max_temperature_c),
             m.min_flow_raw, m.avg_flow_raw, m.max_flow_raw);
}

}  // namespace

//==============================================================================
// app_main
//==============================================================================

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "================================================================");
    ESP_LOGI(TAG, "Flux HAL  ·  Alicat BASIS-2  ·  full-API example");
    ESP_LOGI(TAG, "================================================================");

    // ─── 1) Bring up Flux HAL (best-effort) ─────────────────────────────────
    auto& flux = Flux::GetInstance();
    if (!flux.EnsureInitialized()) {
        ESP_LOGW(TAG, "Flux init failed (this example only needs UART; continuing)");
    } else {
        ESP_LOGI(TAG, "Flux HAL initialised");
    }

    // ─── 2) Configure the UART for RS-485 half-duplex ──────────────────────
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
    if (!BringUpUart(uart)) return;

    // Single bus mutex shared by every handler on this UART.
    static RtosMutex bus_mutex;

    // Callback the handler uses to retune the host UART during multi-baud
    // discovery / normalisation.
    auto set_host_baud = [&uart](std::uint32_t bps) -> bool {
        const bool ok = uart.SetBaudRate(bps);
        if (!ok) {
            ESP_LOGW(TAG, "    set_host_baud(%u) FAILED", static_cast<unsigned>(bps));
        }
        return ok;
    };

    // ─── 3a) Multi-baud discovery sweep ────────────────────────────────────
    //
    // Per the BASIS-2 datasheet each instrument may have been previously
    // configured to one of {4800, 9600, 19200, 38400, 57600, 115200} bps.
    // The handler iterates over every supported baud, retuning the host
    // UART through the callback above, and reports back what was found
    // and at what baud.
    std::array<AlicatBasis2Handler::DiscoveredDevice, cfg::kMaxMeters> found{};
    std::size_t n_found = 0;

    auto probe_cfg = MakeDefaultBasis2Config(/*addr=*/1, /*idx=*/0);
    static AlicatBasis2Handler probe(uart, probe_cfg, &bus_mutex);

    if (cfg::kNormalizeBus) {
        ESP_LOGI(TAG, "Multi-baud discovery sweep …");
        const auto rc = probe.DiscoverAcrossBauds(found.data(), found.size(),
                                                  set_host_baud,
                                                  /*probe_timeout_ms=*/40,
                                                  /*settle_ms=*/25);
        if (!rc.ok()) {
            ESP_LOGW(TAG, "DiscoverAcrossBauds failed: %s — falling back to single-baud probe",
                     al::ToString(rc.error).data());
        } else {
            n_found = rc.value;
            ESP_LOGI(TAG, "Multi-baud sweep: %u instrument(s) responded across all bauds",
                     static_cast<unsigned>(n_found));
            for (std::size_t i = 0; i < n_found; ++i) {
                ESP_LOGI(TAG, "    addr=%u present @ %u bps",
                         found[i].address, static_cast<unsigned>(found[i].baud_bps));
            }
        }

        // ─── 3b) Normalise the whole bus to one baud ───────────────────────
        if (n_found > 0) {
            std::uint8_t fail_bm[32] = {0};
            const auto nz = probe.NormalizeBusBaud(cfg::kNormalizedBaud,
                                                   found.data(), n_found,
                                                   set_host_baud,
                                                   fail_bm, sizeof(fail_bm),
                                                   /*verify_timeout_ms=*/100);
            if (nz.ok()) {
                ESP_LOGI(TAG, "NormalizeBusBaud(%u): %u/%u devices verified at target",
                         static_cast<unsigned>(cfg::kNormalizedBaud),
                         static_cast<unsigned>(nz.value),
                         static_cast<unsigned>(n_found));
                for (std::uint16_t a = 1; a <= 247; ++a) {
                    if (fail_bm[a / 8] & (1U << (a % 8))) {
                        ESP_LOGW(TAG, "    addr=%u FAILED to normalise", a);
                    }
                }
            } else {
                ESP_LOGW(TAG, "NormalizeBusBaud failed: %s", al::ToString(nz.error).data());
            }
        }
    }

    // ─── 3c) Single-baud confirmation sweep at the normalised baud ─────────
    if (!set_host_baud(cfg::kNormalizedBaud)) {
        ESP_LOGE(TAG, "Failed to bring host UART to %u bps — aborting",
                 static_cast<unsigned>(cfg::kNormalizedBaud));
        return;
    }

    std::array<std::uint8_t, 32> present{};
    const auto disc = probe.Discover(present.data(), present.size(), /*timeout_ms=*/40);
    if (!disc.ok()) {
        ESP_LOGW(TAG, "Discover() at %u bps failed: %s — assuming addr=1",
                 static_cast<unsigned>(cfg::kNormalizedBaud),
                 al::ToString(disc.error).data());
        present[0] |= (1U << 1);
    } else {
        ESP_LOGI(TAG, "Final bus map @ %u bps: %u instrument(s)",
                 static_cast<unsigned>(cfg::kNormalizedBaud), disc.value);
        for (uint16_t a = 1; a <= 247; ++a) {
            if (present[a / 8] & (1U << (a % 8))) {
                ESP_LOGI(TAG, "    addr=%u present", a);
            }
        }
    }

    // ─── 4) Build one handler per discovered address ───────────────────────
    std::vector<std::unique_ptr<AlicatBasis2Handler>> meters;
    meters.reserve(cfg::kMaxMeters);

    std::uint8_t logical_index = 0;
    for (uint16_t a = 1; a <= 247 && meters.size() < cfg::kMaxMeters; ++a) {
        if (!(present[a / 8] & (1U << (a % 8)))) continue;

        // Re-use the probe handler for address 1 to avoid creating two
        // handlers for the same slave.
        if (a == 1) {
            meters.emplace_back(nullptr);  // placeholder; we'll use &probe directly below
            ++logical_index;
            continue;
        }

        auto cfg_n = MakeDefaultBasis2Config(static_cast<std::uint8_t>(a), logical_index++);
        meters.emplace_back(std::make_unique<AlicatBasis2Handler>(uart, cfg_n, &bus_mutex));
    }

    // Helper: for-each that yields a reference to every active handler.
    auto for_each = [&](auto&& fn) {
        for (auto& h : meters) {
            AlicatBasis2Handler& ref = h ? *h : probe;
            fn(ref);
        }
    };

    // ─── 5) Configuration walkthrough (every present instrument) ───────────
    for_each([](AlicatBasis2Handler& h) {
        if (!h.EnsureInitialized()) {
            ESP_LOGE(TAG, "addr=%u: EnsureInitialized() failed (powered? wired?)",
                     h.Address());
            return;
        }
        PrintIdentity(h.Identity());
        ConfigurationWalkthrough(h);
    });

    // ─── 6) Measurement-window demo on the first present instrument only ──
    if (!meters.empty()) {
        MeasurementWindowDemo(meters.front() ? *meters.front() : probe);
    }

    // ─── 7) Steady-state read loop ─────────────────────────────────────────
    ESP_LOGI(TAG, "================================================================");
    ESP_LOGI(TAG, "Entering periodic ReadInstantaneous() loop @ %" PRIu32 " ms "
                  "(identity heartbeat every %" PRIu32 " iterations)",
             cfg::kReadIntervalMs, cfg::kIdentityHeartbeatN);
    ESP_LOGI(TAG, "================================================================");

    uint32_t iter = 0;
    while (true) {
        for_each([](AlicatBasis2Handler& h) {
            const auto live = h.ReadInstantaneous();
            if (live.ok()) {
                PrintInstantaneous(h, live.value);
            } else {
                ESP_LOGW(TAG, "  [addr=%u] ReadInstantaneous failed: %s",
                         h.Address(), al::ToString(live.error).data());
            }
        });

        if ((++iter % cfg::kIdentityHeartbeatN) == 0) {
            for_each([](AlicatBasis2Handler& h) {
                const auto rc = h.RereadIdentity();
                if (rc.ok()) {
                    ESP_LOGI(TAG, "  [addr=%u] identity heartbeat ok (fw %u.%u.%u)",
                             h.Address(), rc.value.fw_major,
                             rc.value.fw_minor, rc.value.fw_patch);
                } else {
                    ESP_LOGW(TAG, "  [addr=%u] identity heartbeat FAILED: %s",
                             h.Address(), al::ToString(rc.error).data());
                }
            });
        }

        vTaskDelay(pdMS_TO_TICKS(cfg::kReadIntervalMs));
    }
}
