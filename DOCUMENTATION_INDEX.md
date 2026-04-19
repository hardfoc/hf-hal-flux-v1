# HardFOC HAL Flux Documentation Index

<div align="center">

![Documentation](https://img.shields.io/badge/documentation-complete-green.svg)
![Version](https://img.shields.io/badge/version-3.0-blue.svg)
![Status](https://img.shields.io/badge/status-up--to--date-brightgreen.svg)

**Comprehensive documentation guide for the HardFOC Flux Hardware Abstraction Layer**

[🚀 Quick Start](#quick-start) • [📚 Component Handlers](#component-handlers) • [🔧 Driver Handlers](#driver-handlers) • [🏗️ Architecture](#architecture) • [🔌 API Reference](#api-reference)

</div>

## 📋 Overview

This documentation index provides comprehensive access to all HardFOC Flux HAL system documentation. The Flux board is a valve-control platform based on ESP32-S3 (Xtensa LX7, dual-core, 240 MHz) that drives solenoid valves (MAX22200) and proportional valves (TLE92466ED).

## 🚀 Quick Start

### Essential Documentation
- **[🏠 Main README](README.md)** - Project overview and quick start guide
- **[⚡ Quick Start Examples](#quick-start-examples)** - Get up and running in minutes
- **[🔧 System Integration](lib/api/Flux.h)** - Integration with existing projects

### Quick Start Examples

```cpp
// Minimal HardFOC Flux initialization
#include "api/Flux.h"

int main() {
    // Get the Flux API instance
    auto& flux = Flux::GetInstance();

    // Initialize the complete system
    if (!flux.EnsureInitialized()) {
        printf("System initialization failed\n");
        return -1;
    }

    // Use GPIO
    auto& gpio = GpioManager::GetInstance();
    gpio.SetPin("ESP32_GPIO_2", true);

    // Use Valves
    auto& valves = ValveManager::GetInstance();
    valves.EnsureInitialized();

    // Main loop with health monitoring
    while (true) {
        auto diagnostics = flux.GetSystemDiagnostics();
        if (!diagnostics.system_healthy) {
            printf("System health check failed\n");
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
```

## 📚 Component Handlers

Component handlers provide high-level management interfaces for system resources. Each handler is a singleton that manages multiple hardware sources through a unified API.

### Core System Managers

| Component | Purpose | Hardware Sources | Documentation |
|-----------|---------|------------------|---------------|
| **🎛️ GpioManager** | GPIO pin management | ESP32-S3 | `lib/managers/GpioManager.h` |
| **📊 AdcManager** | ADC channel management | ESP32-S3 (VBUS monitor, NTC) | `lib/managers/AdcManager.h` |
| **📡 CommChannelsManager** | Communication interfaces | SPI/I2C/UART/CAN-TWAI | `lib/managers/CommChannelsManager.h` |
| **🌡️ TemperatureManager** | Temperature monitoring | NTC thermistors | `lib/managers/TemperatureManager.h` |
| **💡 LedManager** | LED control system | WS2812 | `lib/managers/LedManager.h` |
| **🔧 ValveManager** | Valve control system | MAX22200, TLE92466ED | `lib/managers/ValveManager.h` |

### Component Handler Features

#### GpioManager - GPIO Management
- **ESP32-S3 Pins**: Direct GPIO control
- **String-Based API**: Flexible pin identification
- **Thread-Safe Operations**: Concurrent access from multiple tasks

#### AdcManager - Unified ADC System
- **VBUS Monitoring**: Battery/supply voltage monitoring
- **NTC Thermistor**: 10 kΩ NTC temperature sensing
- **Calibration System**: Automatic voltage conversion

#### ValveManager - Valve Control System
- **Solenoid Valves**: MAX22200 driver support
- **Proportional Valves**: TLE92466ED driver support
- **Dynamic Control**: Runtime valve state management
- **Safety Features**: Over-current protection and diagnostics

#### CommChannelsManager - Communication Interfaces
- **CAN/TWAI**: Controller Area Network bus communication
- **SPI**: Serial Peripheral Interface (SPI2)
- **I2C**: Inter-Integrated Circuit bus
- **UART**: Serial communication

## 🔧 Driver Handlers

### Hardware Device Drivers

| Driver | Device | Interface | Documentation |
|--------|--------|-----------|---------------|
| **📐 As5047uHandler** | AS5047U Position Encoder | SPI | **[📖 AS5047U Guide](docs/driver-handlers/AS5047U_HANDLER_README.md)** |

### Encoder System
- **[📖 Encoder Manager Guide](docs/component-handlers/ENCODER_MANAGER_README.md)** - Position encoder system documentation

## 🏗️ Architecture

### System Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                      HardFOC Flux HAL                          │
├─────────────────────────────────────────────────────────────────┤
│  📌 API Layer           │ Flux API — unified singleton          │
├─────────────────────────────────────────────────────────────────┤
│  🎛️ Component Handlers  │ GPIO, ADC, Comm, Temp, LED, Valve     │
├─────────────────────────────────────────────────────────────────┤
│  🔧 Driver Handlers     │ AS5047U, MAX22200, TLE92466ED         │
├─────────────────────────────────────────────────────────────────┤
│  ⚙️ Hardware Drivers    │ ESP32-S3 interfaces & core drivers    │
└─────────────────────────────────────────────────────────────────┘
```

### Architecture Principles
- **Singleton Managers**: Each manager is a singleton with `GetInstance()`
- **`EnsureInitialized()` → `bool`**: All managers return `true`/`false` for init status
- **CommError Enum**: `CommChannelsManager` uses typed `CommError` for communication failures
- **Thread Safety**: All managers are safe for concurrent access via RTOS mutexes

## 🔌 API Reference

### Core API
```cpp
#include "api/Flux.h"    // Unified Flux API
```

### Manager Access Patterns
```cpp
// Singleton access
auto& gpio = GpioManager::GetInstance();
auto& adc = AdcManager::GetInstance();
auto& temp = TemperatureManager::GetInstance();
auto& led = LedManager::GetInstance();
auto& valves = ValveManager::GetInstance();
auto& comm = CommChannelsManager::GetInstance();

// Initialization — all return bool
gpio.EnsureInitialized();
adc.EnsureInitialized();
temp.EnsureInitialized();
led.EnsureInitialized();
valves.EnsureInitialized();
comm.EnsureInitialized();
```

## 🧪 Testing and Validation

### ESP32 On-Target Test Suite

All testing runs on-target via the `examples/esp32/` framework on ESP32-S3 hardware.

- **[📦 ESP32 Test Suite](examples/esp32/README.md)** - On-target test framework
- **[🔧 Test Framework Header](examples/esp32/main/TestFramework.h)** - Lightweight `TEST_ASSERT` macros
- **[🚀 API Integration Tests](examples/esp32/main/flux_api_test.cpp)** - Full Flux API smoke tests

### Running Tests

```bash
cd examples/esp32
./scripts/build_app.sh --app-type example --build-type debug \
    --app-source-file main/flux_api_test.cpp --target esp32c6
```

## 🔄 Continuous Integration

### CI Pipelines (`.github/workflows/`)

| Workflow | Trigger | Purpose |
|----------|---------|---------|
| **🔨 ESP32 Build CI** | push/PR → main | Builds `examples/esp32` on-target test suite |
| **🧹 C++ Lint** | push/PR → main | clang-format checks against `_config/.clang-format` |
| **🔍 C++ Analysis** | push/PR → main | clang-tidy static analysis via `_config/.clang-tidy` |
| **📝 Markdown Lint** | push/PR → main | markdownlint via `_config/.markdownlint.json` |
| **📋 YAML Lint** | push/PR → main | yamllint via `_config/.yamllint` |
| **🔗 Docs Link Check** | push/PR → main | lychee link validation via `_config/lychee.toml` |
| **📖 Docs Publish** | push → main/release/tags | Doxygen + Jekyll → GitHub Pages |
| **📦 Release** | tag `v*` | Automated release packaging |

### Documentation Generation

Doxygen API docs and Jekyll site are configured under `_config/`:
- **[Doxyfile](_config/Doxyfile)** — C++ API docs for `lib/api/` and `lib/managers/`
- **[Jekyll _config.yml](_config/_config.yml)** — Just-the-docs theme with dark mode
- **[Layouts](_config/_layouts/)** — Custom layouts: minimal, print, workflow
- **[Includes](_config/_includes/)** — Custom head/footer HTML fragments
- **[doxygen-awesome-css](_config/doxygen-extensions/doxygen-awesome-css/)** — Doxygen theme submodule

## 🛠️ Development and Contributing

### Development Guidelines
- **[📝 Coding Standards](docs/development/CODING_STANDARDS.md)** — Code style and conventions
- **[🏗️ Architecture Guidelines](docs/development/ARCHITECTURE_GUIDELINES.md)** — System design principles
- **[🔧 CMake Build Contract](docs/development/CMAKE_BUILD_CONTRACT.md)** — Build system rules and targets
- **[🧪 Testing Requirements](docs/development/TESTING_REQUIREMENTS.md)** — Test coverage and quality
- **[⚡ Embedded Coding Standards](docs/development/EMBEDDED_CODING_STANDARDS.md)** — Embedded-specific best practices
- All managers return `bool` from `EnsureInitialized()`
- Use `CommError` enum for communication failures in `CommChannelsManager`
- Thread-safe access via RTOS mutexes in all handlers

### Contribution Workflow
1. **Fork Repository** - Create your fork
2. **Create Feature Branch** - Isolate changes
3. **Follow Standards** - Adhere to coding and documentation standards
4. **Run Tests** - Build and run `examples/esp32` test suite
5. **Submit Pull Request** - Submit for review

## 🔗 Related HALs

- **[HardFOC Vortex HAL](https://hardfoc.github.io/hf-hal-vortex-v1)** — Motor control HAL (8 managers: GPIO, ADC, Comm, Motor, IMU, Temp, LED, Encoder)

---

<div align="center">

**📖 HardFOC Flux HAL Documentation**

**[⭐ Star the Project](https://github.com/hardfoc/hf-hal-flux-v1)** • **[🐛 Report Issues](https://github.com/hardfoc/hf-hal-flux-v1/issues)**

*Last updated: June 2025 | Version 3.0 | Status: Complete*

</div>
