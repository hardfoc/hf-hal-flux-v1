# ADR 0004: Create MCU objects in a per-family platform binding; keep managers MCU-free

| | |
| --- | --- |
| Status | Accepted |
| Date | 2026-11-02 |

## Context

[ADR 0001](0001-mcu-agnostic-boundary.md) put the MCU behind the `Base*` interfaces but let five managers name and
construct the ESP32 implementations (`EspGpio`, `EspSpiBus`, `EspAdc`, `EspCan`, `EspUart`, `EspNvs`,
`EspTemperature`). Their headers included ESP32 wrapper headers, `LedManager` used the ESP-IDF RMT WS2812 driver
and `gpio_num_t`, and `Flux.cpp` called `esp_reset_reason()`, `heap_caps_*` and `esp_restart()`. An STM32 port would
have edited every one of those managers, the product compiled against ESP types through the HAL headers, and no
manager could be compiled, let alone tested, without ESP-IDF. Nothing checked the rule.

## Options

1. **Keep ADR 0001 and port by editing the managers.** No work now; every port and every host test pays for it.
2. **`#if` the managers per MCU.** One file, two platforms interleaved; the host build needs a third branch.
3. **A platform binding per MCU family behind one MCU-free interface**, as pw-hal's `bsp_bind/`. Managers build
   plain-data specs from the pin map and board constants and get `Base*` objects back; each family implements the
   interface once, in its own directory.

## Decision

Option 3. `lib/managers/bsp_bind/FluxPlatformBind.h` declares the factory and system functions;
`lib/managers/bsp_bind/esp32s3/` implements them with the `Esp*` wrappers and ESP-IDF, `lib/managers/bsp_bind/host/`
with the hf-core host mocks. Board wiring that is not in the pin map (bus controllers, clocks, modes, bit rates) is
in `lib/managers/bsp_bind/board/flux_v1/`. ESP-IDF, FreeRTOS and `Esp*` code may appear only in a family
directory, enforced by `tools/lint/lint_hal_boundary.sh` in CI with a shrink-only allowlist. This supersedes the
part of ADR 0001 that placed the `Esp*` construction in the managers; the rest of ADR 0001 stands.

## Consequences

- An MCU port is a new `bsp_bind/<family>/` and a pin map. Managers, handlers and applications do not change.
- The managers, the facade and the board binding build on a host (`tests/host`), which is what the HAL unit tests
  run on.
- Objects handed out by a binding live in fixed static slots, so the binding is also where the "no heap after
  init" rule ([ADR 0002](0002-static-allocation.md)) is easiest to hold for bus and pin objects.
- Applications that relied on ESP types arriving through HAL headers must include them themselves.
  `CommChannelsManager::GetI2cBus()` now returns `BaseI2c*` and `LedManager::GetCurrentGpioPin()` returns
  `hf_pin_num_t`.
