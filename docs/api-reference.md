# API reference

The full C++ API of the HAL, from the Doxygen comments in `lib/api` and `lib/managers`.

[Open the API reference](api/index.html){ .md-button .md-button--primary }

## Start here

| Page | What it is |
| --- | --- |
| [`Flux`](api/class_flux.html) | The board facade: `GetInstance()`, `EnsureInitialized()`, the manager members, system diagnostics |
| [`ValveManager`](api/class_valve_manager.html) | Solenoid and proportional valve channels |
| [`AdcManager`](api/class_adc_manager.html) | ESP32 ADC and ADS7952 channels by name |
| [`CommChannelsManager`](api/class_comm_channels_manager.html) | SPI buses, CAN, UARTs and USB Serial/JTAG |
| [`GpioManager`](api/class_gpio_manager.html) | GPIO pins of the pin map by name |

## Build it locally

Run Doxygen from the repository root and open the `index.html` it writes:

```bash
mkdir -p build/doxygen && doxygen Doxyfile
open build/doxygen/html/index.html    # xdg-open on Linux
```

The published reference uses the HardFOC stylesheet, which the docs workflow adds; a local build uses Doxygen's
default look.
