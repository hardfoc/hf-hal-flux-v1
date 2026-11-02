# ADR 0001: Keep the MCU behind the interface wrap; only managers know the board

| | |
| --- | --- |
| Status | Accepted (records the current design) |
| Date | 2026-11-01 |

## Context

HardFOC boards share chips (the WS2812 LED, NTC sensors, CAN) and a common core (`hf-core`: handlers, drivers,
interface wrap). Flux V1 runs on an ESP32-S3 today. Future boards are expected on other MCUs, in particular an
STM32 with CAN FD, and the same valve and sensor code should move with them.

## Options

1. **Write the HAL directly against ESP-IDF.** Fast to start; every manager and handler would need rewriting for
   another MCU.
2. **Put the MCU behind interface classes and keep board knowledge in one layer.** Chip drivers and handlers use
   only `BaseSpi`, `BaseGpio`, `BaseAdc`, `BaseCan`, `BaseUart`, `BaseNvs`, `BaseTemperature`; the managers are the
   only code that knows which chip is on which pin of which board.

## Decision

Option 2. The boundary is:

- **Drivers and handlers** (in `hf-core`) depend only on the `Base*` interface classes.
- **The ESP32 implementations** (`EspSpiBus`, `EspGpio`, `EspAdc`, `EspCan`, `EspUart`, `EspNvs`,
  `EspTemperature`) are named in exactly the managers that create buses and peripherals: `CommChannelsManager`,
  `GpioManager`, `AdcManager`, `NvsManager`, `TemperatureManager`. The generated
  [manager map](../architecture/ownership.md) lists them, read from the `#include` lines.
- **Board wiring** lives in the per-board pin map in `hf-pincfg` ([ADR 0003](0003-pin-map-ownership.md)).
- **Task placement** uses logical core indices (`FluxCpuAffinity.h`), not vendor core names.

## Consequences

- A port to another MCU replaces the `Esp*` construction in those managers (with `Stm*` or other implementations of
  the same interfaces) and adds a pin map; handlers, drivers and application code above the managers stay.
- **CAN FD** fits the same seam: a future FD-capable `BaseCan` implementation (larger frames, data-phase bit timing)
  is created by `CommChannelsManager` instead of `EspCan`; code that only sees `BaseCan*` keeps working, and code
  that wants FD frames checks the port's capabilities.
- Known leaks across the boundary today, to fix before a port: `Flux.cpp` calls ESP-IDF directly for reset reason,
  heap size and restart (`esp_reset_reason()`, `heap_caps_*`, `esp_restart()`); `LedManager` uses the ESP-IDF RMT
  WS2812 driver and `gpio_num_t`; the valve and ADC SPI host numbers in `CommChannelsManager.cpp` are ESP32 values.
