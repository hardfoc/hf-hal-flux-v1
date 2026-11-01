# Architecture

How the Flux V1 HAL is put together. The short version: your code talks to one object, `Flux`; `Flux` holds one
manager per board function; managers own hf-core handlers; handlers wrap chip drivers; drivers reach the hardware
only through the interface classes of the internal interface wrap, which sit on ESP-IDF.

<!-- figure: hal-layers -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="../diagrams/hal-layers.dark.svg">
  <img src="../diagrams/hal-layers.svg"
    alt="Layer stack of the Flux V1 HAL: application, the Flux facade, 7 managers, 5 hf-core handlers, 6 chip
    drivers, and the interface wrap on ESP-IDF and the ESP32-S3.">
</picture>
<!-- /figure -->

## The layers

| Layer | Where | What it does | You use it when |
| --- | --- | --- | --- |
| Application | your project, [`examples/esp32`](../../examples/esp32) | Tasks, control loops, protocols | Always |
| Facade | [`lib/api/Flux.h`](../../lib/api/Flux.h) | One singleton; brings the managers up in order; system diagnostics | Always: `Flux::GetInstance()` |
| Managers | [`lib/managers`](../../lib/managers) | One per board function (valves, ADC, temperature, ...); know the board's wiring from the pin map | Almost always: `flux.valves`, `flux.adc`, ... |
| hf-core handlers | `lib/core/handlers` | Wrap one chip driver each and adapt it to the interface classes | Rarely; for chip features a manager does not expose |
| Chip drivers | `lib/core/hf-core-drivers/external` | Register-level drivers, one repository per chip | Almost never from application code |
| Interface wrap | `lib/core/hf-core-drivers/internal/hf-internal-interface-wrap` | `BaseSpi`, `BaseGpio`, `BaseAdc`, ... and their ESP32 implementations (`EspSpi`, `EspGpio`, ...) | When porting to another MCU |

The rule that holds the design together: **a layer only calls the layer below it.** The managers are the only code
that knows Flux V1 exists; handlers and drivers are shared with other HardFOC boards. Why the line is drawn there is
recorded in [ADR 0001](../adr/0001-mcu-agnostic-boundary.md).

## Read on

- [Initialisation](initialization.md): the order the managers come up and what happens when one fails.
- [Ownership](ownership.md): which manager creates which handler, and how memory is allocated.
- [Threading](threading.md): which calls are safe from several tasks.
- [Errors and diagnostics](errors.md): error enums, `GetLastError()`, health snapshots.
