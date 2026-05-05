# Flux HAL — architecture (as implemented)

## Layers

Same layered story as the handbook ([layered architecture](../hf-development-handbook/process/layered-architecture.md)): **Flux** → **HAL managers** → **`hf-core` handlers** → **drivers** → **`Base*`** → ESP-IDF.

## `Flux` members

See **`lib/api/Flux.h`** for the full API. Subsystem references:

| Member | Manager | Role |
|--------|---------|------|
| `nvs` | `NvsManager` | Namespaces for cal/config |
| `comms` | `CommChannelsManager` | SPI (valve drivers), CAN, UART pool |
| `gpio` | `GpioManager` | ESP32 GPIO |
| `valves` | `ValveManager` | MAX22200 + TLE92466ED paths |
| `adc` | `AdcManager` | ESP32 ADC (NTC, VBUS, … per board) |
| `leds` | `LedManager` | WS2812 status |
| `temp` | `TemperatureManager` | NTC + internal die / derived |

## Initialization (`Flux::InitializeAllComponents` in `lib/api/Flux.cpp`)

Steps run in order with explicit **fatal** vs **non-fatal** behaviour:

1. **NvsManager** — **fatal** on failure (aborts whole init).
2. **CommChannelsManager** — **fatal**.
3. **GpioManager** — **fatal**.
4. **ValveManager** — **non-fatal** (logged; init sequence continues).
5. **AdcManager** — **non-fatal**.
6. **LedManager** — **non-fatal**.
7. **TemperatureManager** — **non-fatal**.

So `EnsureInitialized()` returns **false** only when **NVS, comms, or GPIO** fails. Downstream subsystem failures still leave `EnsureInitialized()` **true** but with failed-component / warning lists — treat logs and `GetSystemDiagnostics()` as source of truth.

`Shutdown()` reverses the stack (see `Flux.cpp`).

## CPU affinity

Logical CPU indices for tasks are documented in **`lib/api/FluxCpuAffinity.h`** and referenced from `Flux.h`.

## Deeper reading

| Topic | Where |
|--------|--------|
| Valve + pressure demo | [`examples/esp32/main/manifold_valves_pressure_control/README.md`](../../examples/esp32/main/manifold_valves_pressure_control/README.md) |
| Example app index | [`examples/esp32/docs/README.md`](../../examples/esp32/docs/README.md) |
