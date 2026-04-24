# HardFOC Flux V1 – Hardware Abstraction Layer

ESP32 HAL for the **HardFOC Flux V1** valve-control board. Drives solenoid valves (MAX22200) and proportional valves (TLE92466ED) through a unified, stack-based manager architecture.

## Engineering handbook

Cross-HAL **coding standards, architecture, CMake contract, testing, and agent playbooks** live in the [hf-development-handbook](https://github.com/hardfoc/hf-development-handbook) Git submodule at [`docs/hf-development-handbook/`](docs/hf-development-handbook/). After clone, run `git submodule update --init --recursive`. A short index with relative links is in [`docs/development/README.md`](docs/development/README.md).

---

## Board Overview

| Feature                | Detail                                           |
|------------------------|--------------------------------------------------|
| **MCU**                | ESP32                                            |
| **Solenoid Driver**    | MAX22200 – 8-channel octal serial-controlled     |
| **Proportional Driver**| TLE92466ED – 6-channel low-side switch           |
| **Status LED**         | WS2812 (NeoPixel), 1 LED, RMT channel 0         |
| **Temperature**        | 10 kΩ NTC thermistor (B=3950) + ESP32 internal   |
| **Voltage Monitor**    | VBUS via 11× resistor divider on ADC1_CH0        |
| **Communication**      | CAN/TWAI (500 kbps), I²C (400 kHz), SPI2 host   |

---

## Pin Map

### SPI Bus (SPI2 – shared)

| Signal | GPIO |
|--------|------|
| MOSI   | 6    |
| MISO   | 7    |
| SCLK   | 2    |

### MAX22200 (Solenoid Driver)

| Signal  | GPIO |
|---------|------|
| CS      | 18   |
| ENABLE  | 11   |
| CMD     | 10   |
| FAULT_N | 5    |

### TLE92466ED (Proportional Driver)

| Signal  | GPIO |
|---------|------|
| CS      | 19   |
| RESN    | 16   |
| EN      | 17   |
| FAULT_N | 8    |

### CAN / TWAI

| Signal | GPIO |
|--------|------|
| TX     | 14   |
| RX     | 15   |

### I²C

| Signal | GPIO |
|--------|------|
| SDA    | 21   |
| SCL    | 22   |

### Analog

| Channel           | GPIO | ADC       | Divider |
|--------------------|------|-----------|---------|
| NTC Thermistor     | 4    | ADC1_CH4  | 1.0×    |
| VBUS Monitor       | 0    | ADC1_CH0  | 11.0×   |

### LED

| Signal  | GPIO |
|---------|------|
| WS2812  | 3    |

---

## Architecture

```
┌──────────────────────────────────────────────┐
│                   Flux API                   │   Unified singleton entry point
│               (Flux.h / Flux.cpp)            │
├──────────┬──────────┬──────────┬─────────────┤
│ CommChan │ GpioMgr  │ ValveMgr │ AdcMgr      │
│ Manager  │          │          │             │   Managers
│          │          │          │ LedMgr      │
│          │          │          │ TempMgr     │
├──────────┴──────────┴──────────┴─────────────┤
│              Handler Layer (core)            │   Max22200Handler, Tle92466edHandler,
│                                              │   NtcTemperatureHandler, WS2812, Logger
├──────────────────────────────────────────────┤
│           Interface Wrappers (core)          │   EspSpi, EspI2c, EspGpio, EspAdc, EspCan,
│                                              │   EspTemperature, EspNvs, EspLogger
└──────────────────────────────────────────────┘
```

### Design Principles

- **Stack-based allocations** – Fixed-size `std::array` registries, `constexpr` mapping tables, linear scans for small-N lookups. Heap allocation only where ESP-IDF driver APIs require it (handler construction, WS2812 RMT buffer).
- **Singleton managers** – Each manager is a Meyers singleton, initialized lazily on first access through `Flux::EnsureInitialized()`.
- **CRTP SPI adapters** – External driver handlers use the Curiously Recurring Template Pattern for zero-overhead polymorphism.
- **X-MACRO pin config** – All pin/ADC definitions live in `hf_functional_pin_config_flux_v1.hpp`, auto-generating enums, string tables, and mapping arrays.
- **Shared `core/` submodule** – Handlers, interface wrappers, and utilities are symlinked from the Vortex HAL's core.

---

## Managers

### CommChannelsManager

Initialises the shared SPI2 bus with two devices (MAX22200 @ Mode 0/10 MHz, TLE92466ED @ Mode 1/5 MHz) and CAN/TWAI at 500 kbps. Provides lazy `AcquireUart()` for serial sensors (BASIS-2, FDO2). Also exposes an opt-in `AcquireUsbSerialJtag()` accessor for the ESP32-S3 native-USB CDC-ACM/JTAG console — gated by the CMake flag `HF_CORE_ENABLE_USB_SERIAL_JTAG` (OFF by default; returns `nullptr` when disabled). See [`COMM_CHANNELS_MANAGER_README`](docs/component-handlers/COMM_CHANNELS_MANAGER_README.md).

### GpioManager

Manages ESP32 GPIOs only (no I/O expander). Reads pin definitions from the X-MACRO pin config and registers GPIO + USER category pins. Provides name-based and enum-based access.

### ValveManager

Unified interface to both valve drivers:

- **Solenoid channels** (`SOL_CH0`–`SOL_CH7`): MAX22200 enable/disable, CDR/VDR current configuration, chopping frequency.
- **Proportional channels** (`PROP_CH0`–`PROP_CH5`): TLE92466ED current setpoint (mA), PWM frequency, soft-start.
- Fault detection, channel diagnostics, combined status reporting.

### AdcManager

Two-channel ESP32 ADC1 manager:
- NTC thermistor raw/voltage readings (voltage divider = 1.0×)
- VBUS monitor with 11× divider compensation

### LedManager

WS2812 LED with animation engine:
- Solid, blink, breathe, rainbow animations
- Status indicators: boot (blue blink), ready (green), warning (yellow blink), error (red fast-blink), calibrating (cyan breathe)

### TemperatureManager

Dual-source temperature monitoring:
- ESP32 internal die sensor
- NTC thermistor via ADC, using `NtcTemperatureHandler` (10 kΩ, B=3950, 10 kΩ pull-up)
- `GetSystemTemperatureStats()` for aggregate min/max/average

---

## API Changes (v2.0.0)

| Area | Change |
|------|--------|
| **Diagnostics types** | `FluxAdcDiagnostics` → `AdcSystemDiagnostics`, `FluxTempDiagnostics` → `TempSystemDiagnostics` |
| **Return types** | `GetSystemDiagnostics()` returns typed error codes (`hf_adc_err_t`, `hf_valve_err_t`, `hf_gpio_err_t`) instead of `bool` |
| **Temperature** | `ReadTemperatureCelsius()` etc. return `TemperatureError` instead of `bool` |
| **Error tracking** | All managers now expose `GetLastError()` |
| **GPIO diagnostics** | New `GpioSystemDiagnostics` struct with interrupt counting |
| **GPIO interrupts** | `EnableInterrupt()`, `DisableInterrupt()`, `RegisterInterruptCallback()`, `GetInterruptCount()` |
| **Lifecycle** | `Flux::Shutdown()` for graceful reverse-order teardown |
| **Convenience** | `GetFlux()` free function (equivalent to `Flux::GetInstance()`), `GetCommChannelsManager()` |

---

## Quick Start

```cpp
#include "api/Flux.h"

void app_main() {
    auto& flux = hf::hal::flux::GetFlux();
    flux.EnsureInitialized();

    // Enable solenoid channel 0
    flux.valves.EnableChannel(ValveManager::ValveChannel::SOL_CH0);

    // Set proportional valve current
    flux.valves.SetProportionalCurrent(
        ValveManager::ValveChannel::PROP_CH0, 500);  // 500 mA

    // Read board temperature
    float temp = 0.0f;
    flux.temp.ReadTemperatureCelsius("NTC_THERMISTOR", temp);

    // Set LED to solid green
    flux.leds.SetAnimation(LedManager::LedAnimation::SOLID,
                           {0, 255, 0});

    // Print system info
    flux.DumpSystemStatistics();
}
```

---

## Build

This component is designed for the ESP-IDF build system (v5.0+). Add it to your project's `components/` directory or reference it via `EXTRA_COMPONENT_DIRS`:

```cmake
set(EXTRA_COMPONENT_DIRS
    "/path/to/hf-all-hals/hf-hal-flux-v1"
)
```

Target: `esp32c6`

```bash
idf.py set-target esp32c6
idf.py build
```

---

## Directory Structure

```
hf-hal-flux-v1/
├── CMakeLists.txt              # ESP-IDF component build
├── idf_component.yml           # Component manifest
├── README.md                   # This file
├── LICENSE
├── lib/
│   ├── api/
│   │   ├── Flux.h              # Unified API singleton
│   │   ├── Flux.cpp
│   │   └── FluxVersion.h       # Version constants
│   ├── managers/
│   │   ├── CommChannelsManager.h/.cpp
│   │   ├── GpioManager.h/.cpp
│   │   ├── ValveManager.h/.cpp
│   │   ├── AdcManager.h/.cpp
│   │   ├── LedManager.h/.cpp
│   │   └── TemperatureManager.h/.cpp
│   └── core/ -> ../../hf-hal-vortex-v1/lib/core  (symlink)
│       ├── handlers/            # Max22200, Tle92466ed, NTC, WS2812, Logger
│       ├── hf-core-drivers/     # ESP32 interface wrappers + external drivers
│       └── hf-core-utils/       # RTOS wrappers, CAN utils, general utilities
```

---

## License

See [LICENSE](LICENSE).
