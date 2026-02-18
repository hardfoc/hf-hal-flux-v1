# HardFOC Flux — Development Guidelines

<div align="center">

![Standards](https://img.shields.io/badge/standards-shared-blue.svg)
![C++20](https://img.shields.io/badge/C%2B%2B-20-brightgreen.svg)
![ESP-IDF](https://img.shields.io/badge/ESP--IDF-5.4%2F5.5-orange.svg)

</div>

## 📋 Overview

The HardFOC Flux HAL follows the same development standards as the Vortex HAL. The authoritative development guides live in the **[hf-hal-vortex-v1 docs/development/](https://github.com/hardfoc/hf-hal-vortex-v1/tree/main/docs/development)** directory and apply equally to both HALs.

## 📚 Shared Development Guides

| Guide | Description |
|-------|-------------|
| **Architecture Guidelines** | HAL layer architecture, singleton patterns, init flow |
| **Coding Standards** | Naming conventions, formatting, comment style |
| **Embedded Coding Standards** | ESP32 / FreeRTOS specific practices |
| **BaseInterface Reference** | `BaseGpio`, `BaseAdc`, `BaseTemperature` contracts |
| **Component Manifest Guide** | `idf_component.yml` structure and versioning |
| **Documentation Standards** | README templates, Doxygen annotations |
| **Performance Optimization** | String lookups vs cached access patterns |
| **Testing Requirements** | On-target test framework, coverage expectations |

## 🔧 Flux-Specific Conventions

### Manager Patterns

All Flux managers follow the same singleton + `EnsureInitialized()` → `bool` pattern:

```cpp
class ExampleManager {
public:
    static ExampleManager& GetInstance();
    bool EnsureInitialized();

private:
    ExampleManager() = default;
    bool initialized_{false};
};
```

### Memory Ownership

Flux managers use a **`void*` opaque storage** pattern to avoid heavy header dependencies:

```cpp
// In .h (no driver header required)
void* driver_storage_{nullptr};
BaseDriver* driver_{nullptr};  // Non-owning typed alias

// In .cpp
auto* d = new (std::nothrow) ConcreteDriver(config);
driver_storage_ = d;
driver_ = d;
```

This is intentional — singletons live for the program lifetime, so cleanup is handled by OS teardown. Managers that support explicit cleanup (e.g., `LedManager::Shutdown()`) document it.

### Error Handling

| Manager | Error Type | Notes |
|---------|-----------|-------|
| **ValveManager** | `hf_valve_err_t` | Typed enum with driver error mapping |
| **CommChannelsManager** | `CommError` | 12-value enum class |
| **All others** | `bool` | `true` = success |

### Valve-Specific Patterns

The Flux board's primary function is valve control. `ValveManager` uses compile-time channel mapping:

```cpp
// O(1) channel resolution via constexpr array
static constexpr ValveChannelMapping kValveChannelMap[] = {
    {0, ValveDriver::kMax22200, 0},   // Solenoid channel 0
    // ...
    {8, ValveDriver::kTle92466, 0},   // Proportional channel 0
};
```

## 🏗️ Build System

```bash
# Build on-target tests
cd examples/esp32
./scripts/build_app.sh --app-type example --build-type debug \
    --app-source-file main/flux_api_test.cpp --target esp32c6
```

## 🔗 Related

- **[Flux DOCUMENTATION_INDEX](../../DOCUMENTATION_INDEX.md)** — Full documentation map
- **[Flux API](../../lib/api/Flux.h)** — Unified API header
- **[Vortex Development Docs](https://github.com/hardfoc/hf-hal-vortex-v1/tree/main/docs/development)** — Shared standards (authoritative)
