# Getting started

This page takes you from nothing to a Flux V1 board printing its start-up log. Each step says how to tell that it
worked. Plan on half an hour, most of it the ESP-IDF download.

## What you need

| Item | Notes |
| --- | --- |
| A Flux V1 board | Any revision with the ESP32-S3 module |
| USB cable | Data-capable; the board enumerates as a USB serial / JTAG device |
| A 24 V valve supply | Only for driving valves. The board starts and logs without it |
| Linux, macOS or Windows (WSL2) | The build scripts are Bash |

## 1. Install ESP-IDF

The examples target `esp32s3` and build with ESP-IDF **release/v5.5** (both read from
[`examples/esp32/app_config.yml`](../examples/esp32/app_config.yml), which CI uses too).

```bash
mkdir -p ~/esp && cd ~/esp
git clone -b release/v5.5 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf && ./install.sh esp32s3
. ./export.sh
```

**It worked if** `idf.py --version` prints a v5.5 version. Run `. ~/esp/esp-idf/export.sh` in every new terminal.

## 2. Clone with submodules

The HAL pulls the shared core (`lib/core`: handlers, drivers, the pin map) and the build scripts as git submodules.

```bash
git clone --recurse-submodules https://github.com/hardfoc/hf-hal-flux-v1.git
cd hf-hal-flux-v1
```

If you already cloned without submodules:

```bash
git submodule update --init --recursive
```

**It worked if** `ls lib/core/handlers` lists handler folders and `ls examples/esp32/scripts` shows `build_app.sh`.

!!! note "One submodule is private"
    `docs/hf-development-handbook` is a private repository and is marked `update = none`, so the recursive update
    skips it. You do not need it to build.

## 3. Build an example

Every example is an "app" selected by name. Start with `flux_api_test`, the default app, which initialises the whole
HAL and checks each manager.

```bash
cd examples/esp32
./scripts/build_app.sh list                      # every app name
./scripts/build_app.sh flux_api_test Release     # build one
```

**It worked if** the script ends with a successful link and a build folder named after the app, the build type and
the target appears under `examples/esp32/`.

## 4. Flash and monitor

Connect the board over USB, then:

```bash
./scripts/flash_app.sh flash_monitor flux_api_test Release
```

Set `ESPPORT` first if more than one serial port is present (for example `export ESPPORT=/dev/ttyACM0`). Leave the
monitor with ++ctrl+bracket-right++.

**It worked if** the log shows the Flux start-up table: one row per manager (NVS, Comms, GPIO, Valves, ADC, LEDs,
Temp) with `OK`, `WARN` or `FAIL`, then `Flux API initialized in ... ms`. `WARN` on Valves or ADC is normal without
the valve supply or with unfitted parts; see [Initialisation](architecture/initialization.md).

## 5. Use the HAL in your own code

```cpp
#include "api/Flux.h"
#include "managers/ValveManager.h"

extern "C" void app_main() {
    auto& flux = Flux::GetInstance();
    if (!flux.EnsureInitialized()) {
        return;  // NVS, comms or GPIO failed: nothing below can work
    }
    if (flux.valves.IsInitialized()) {
        (void)flux.valves.ConfigureSolenoidCdr(ValveChannel::SOL_CH0, 800, 300, 20.0f);
        (void)flux.valves.EnableChannel(ValveChannel::SOL_CH0);
    }
}
```

## Next steps

- [Learn](learn/index.md) walks through the rest of the site in order.
- [Examples](examples/index.md) lists every app and what it shows.
- [Troubleshooting](guides/troubleshooting.md) covers the common first-day problems.
