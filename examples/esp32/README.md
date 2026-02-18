# HardFOC Flux HAL — ESP32 Examples

## Overview

This directory contains buildable ESP-IDF examples that exercise the **Flux HAL API**
from `lib/api/Flux.h`. Each `.cpp` file in `main/` is a standalone test/example
that can be built and flashed independently using the `build_app.sh` script.

## Available Examples

| App Type | Source File | Description |
|---|---|---|
| `flux_api_test` | `flux_api_test.cpp` | Full Flux API initialization and diagnostics |

## Quick Start

```bash
# Build the default example
./scripts/build_app.sh flux_api_test Release

# Flash to device
./scripts/flash_app.sh

# List all available examples
./scripts/build_app.sh list
```

## Project Structure

```
examples/esp32/
├── CMakeLists.txt          # Project root (requires APP_TYPE from build_app.sh)
├── README.md               # This file
├── sdkconfig.defaults      # ESP-IDF SDK configuration defaults
├── main/
│   ├── CMakeLists.txt      # Component registration
│   ├── TestFramework.h     # Shared test infrastructure
│   └── flux_api_test.cpp   # Full API test
└── scripts/                # Build/flash tooling (submodule)
```

## Build System

This project uses the same build infrastructure as the HardFOC external driver
examples. The `scripts/` directory is a Git submodule pointing to the shared
build tooling repository.

Each example is built by passing `APP_TYPE` and `BUILD_TYPE` to CMake via
`build_app.sh`, which maps the app type to the correct source file.
