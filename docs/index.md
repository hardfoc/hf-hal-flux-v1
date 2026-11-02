---
hide:
  - navigation
  - toc
---

<div class="hf-hero" markdown>

<span class="hf-kicker">HardFOC board HAL</span>

# Flux V1 HAL

The software layer for the HardFOC Flux V1 valve control board. One `Flux` object gives your ESP32-S3 application
eight solenoid valve channels, six proportional valve channels, analog inputs, temperatures, CAN, storage and a
status LED, through a small set of managers that know the board's wiring.

[Quickstart](quickstart.md){ .md-button .md-button--primary }
[Architecture](architecture/index.md){ .md-button }
[Source on GitHub](https://github.com/hardfoc/hf-hal-flux-v1){ .md-button }

</div>

This repository (`hf-hal-flux-v1`) turns the board into C++ objects. Your application does not touch GPIO numbers
or SPI frames; it calls `flux.valves`, `flux.adc`, `flux.temp`.

<div class="hf-chip hf-chip--board" markdown>
<div class="hf-chip__head">
<span class="hf-chip__kicker">Valve control board</span>
<span class="hf-chip__name">Flux V1</span>
<span class="hf-chip__vendor">HardFOC</span>
</div>

MCU
:   ESP32-S3, dual core; ESP-IDF release/v5.5

Valve drivers
:   MAX22200: 8 on/off solenoid channels (hit and hold) · TLE92466ED: 6 proportional channels (regulated current,
    PWM, dither); one SPI bus, 1 MHz

Analog inputs
:   ADS7952: 12 channels, 12 bit, 2.5 V reference, own SPI bus at 4 MHz · ESP32 ADC1: NTC thermistor and supply
    voltage

Communication
:   CAN 500 kbit/s (TWAI) · UART for a PyroScience FDO2 oxygen probe · USB Serial/JTAG (console, flashing)

Power
:   24 V valve supply, needed only to drive valves; the board starts and logs from USB without it

Status
:   One WS2812 RGB LED

[Hardware](hardware/index.md) · [Pin map](hardware/pin-map.md)
</div>

<!-- figure: hal-layers -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="diagrams/hal-layers.dark.svg">
  <img src="diagrams/hal-layers.svg"
    alt="Layer stack of the Flux V1 HAL: application, the Flux facade, 7 MCU-free managers, 5 hf-core
    handlers, 6 chip drivers, the interface classes, and the platform binding that creates their ESP32
    implementations on ESP-IDF and the ESP32-S3.">
</picture>
<!-- /figure -->

## What you get

- One `Flux` object that brings the board up in a fixed order and reports what failed
  ([Initialisation](architecture/initialization.md)).
- One manager per board function: valves, ADC, temperature, GPIO, LED, communication, storage
  ([Managers](managers/index.md)).
- Valve channels by name (`ValveChannel::SOL_CH0`, `PROP_CH0`), whichever chip drives them
  ([ValveManager](managers/valves.md)).
- A pin map generated from the board's pin header, so the pages cannot disagree with the firmware
  ([Pin map](hardware/pin-map.md)).
- Diagnostics for every manager: `GetLastError()`, health snapshots, `DumpSystemStatistics()`
  ([Errors and diagnostics](architecture/errors.md)).
- Example apps for each manager, built by CI on every pull request ([Examples](examples/index.md)).

## Where to go next

<div class="grid cards" markdown>

- :material-rocket-launch-outline: **[Quickstart](quickstart.md)**

    Install ESP-IDF, clone with submodules, build, flash and watch the first example.

- :material-wrench-outline: **[Bring-up and troubleshooting](bring-up.md)**

    The first power-up, step by step, and what each start-up warning means.

- :material-chip: **[Hardware](hardware/index.md)**

    Buses, chips and the pin map, generated from the board's pin header.

- :material-layers-outline: **[Architecture](architecture/index.md)**

    Layers, start-up order, ownership, threading and errors.

- :material-valve: **[Managers](managers/index.md)**

    One page per manager: what it does, its API, an example and its pitfalls.

- :material-play-box-outline: **[Examples](examples/index.md)**

    Every buildable app, generated from `app_config.yml`.

</div>
