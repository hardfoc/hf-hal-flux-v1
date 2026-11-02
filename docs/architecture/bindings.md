# Platform bindings

Where the MCU-specific code lives, and the rule that keeps it there. The managers decide *what* the board needs;
a small platform binding, one per MCU family, creates the objects. Everything above the binding compiles without
ESP-IDF, which is what makes a host test build and a later STM32 port possible.

## Facade, managers, binding, hf-core

| Layer | Where | Knows about | Never includes |
| --- | --- | --- | --- |
| Facade | [`lib/api/Flux.h`](../../lib/api/Flux.h) | The managers, init order, system diagnostics | ESP-IDF, FreeRTOS, `Esp*` |
| Managers | [`lib/managers`](../../lib/managers) (`*Manager.*`) | The board: pins, buses, chips, what a failure means | ESP-IDF, FreeRTOS, `Esp*` |
| Board binding | [`lib/managers/bsp_bind/board/flux_v1`](../../lib/managers/bsp_bind/board/flux_v1) | Bus controllers, clocks, SPI modes, CAN bit rate, LED channel | ESP-IDF, FreeRTOS, `Esp*` |
| Binding interface | [`lib/managers/bsp_bind/FluxPlatformBind.h`](../../lib/managers/bsp_bind/FluxPlatformBind.h) | Plain-data specs in, `Base*` out | ESP-IDF, FreeRTOS, `Esp*` |
| Family binding | [`lib/managers/bsp_bind/esp32s3`](../../lib/managers/bsp_bind/esp32s3) | `EspGpio`, `EspSpiBus`, `EspAdc`, `EspCan`, `EspUart`, `EspNvs`, `EspTemperature`, the WS2812 RMT driver, `esp_restart()` | - |
| hf-core | `lib/core` | `Base*` interfaces, handlers, chip drivers, the `Esp*` wrappers | - |

A manager turns the pin map and the board constants into a spec (`GpioSpec`, `SpiBusSpec`, `SpiDeviceSpec`,
`CanSpec`, `UartSpec`, `LedStripSpec`) and calls the binding, for example `flux::bsp::CreateSpiDevice(bus, spec)`.
The binding returns a `BaseSpi*`, and from then on the manager and the handlers it owns see only the interface.

## Families

Exactly one family is linked into an image; [`CMakeLists.txt`](../../CMakeLists.txt) picks it from `HF_CORE_MCU`.

| Family | Directory | Built when | Objects |
| --- | --- | --- | --- |
| ESP32-S3 | `bsp_bind/esp32s3/` | ESP-IDF build (`HF_CORE_MCU=ESP32`) | `Esp*` wrappers over ESP-IDF |
| Host | `bsp_bind/host/` | Host unit tests | The `Mock*` fakes of hf-core's `hf_core_host_mocks` |

Every object a binding hands out lives in a fixed slot in static storage (one per pin, bus, port or NVS region),
so the binding itself never calls the heap allocator. ESP-IDF drivers that keep queues of their own (SPI master,
TWAI, UART, RMT) allocate them inside the `Create*` call, which runs from the owning manager's
`EnsureInitialized()`. A new MCU family is a new directory implementing every function in `FluxPlatformBind.h`;
no manager changes.

## The boundary lint

[`tools/lint/lint_hal_boundary.sh`](../../tools/lint/lint_hal_boundary.sh) runs in CI (`ci-hal-boundary.yml`) and
fails the pull request when a file in `lib/api` or `lib/managers` outside a family directory:

- includes an ESP-IDF header (`esp_*`, `driver/`, `soc/`, `hal/`, `freertos/`, `FreeRTOS.h`, `sdkconfig.h`, ...),
  an hf-core `Esp*` wrapper or the WS2812 RMT driver;
- includes a family directory (`bsp_bind/esp32s3/`, `bsp_bind/host/`) from outside `bsp_bind/`;
- names a family type in a header (`Esp*`, `gpio_num_t`, `esp_err_t`, `TaskHandle_t`, ...), even in a forward
  declaration.

Known debt goes in [`tools/lint/hal_boundary_allowlist.txt`](../../tools/lint/hal_boundary_allowlist.txt), and the
list may only shrink: a listed file that no longer violates fails the lint until its line is removed, and on a
pull request a line that is not on the base branch fails it too. The list is empty.

```bash
tools/lint/lint_hal_boundary.sh                 # what a developer runs
tools/lint/lint_hal_boundary.sh --base origin/main   # what CI runs on a pull request
```

The decision behind the boundary is [ADR 0001](../adr/0001-mcu-agnostic-boundary.md).
