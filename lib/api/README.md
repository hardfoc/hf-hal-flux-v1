# Flux API — Unified HardFOC Platform Interface

## Overview

The Flux API provides a unified singleton interface to all component managers
in the HardFOC Flux V1 platform. It implements lazy initialization with proper
dependency management, ensuring all subsystems are correctly initialized before
exposing them to the application.

## Architecture

### Design Principles

- **True Singleton Pattern**: Uses Meyer's singleton for thread-safe initialization
- **Lazy Initialization**: Components are initialized only when needed
- **Dependency Management**: Proper initialization order based on component dependencies
- **Exception-Free Design**: All methods are `noexcept` with proper error handling
- **Reference-Based Access**: Direct access to component managers by reference
- **Comprehensive Diagnostics**: Full system health monitoring and diagnostics

### Component Dependencies

The initialization order is carefully managed to handle dependencies:

```
1. NvsManager           (ESP-IDF NVS — `flux` / `flux_cal` / `flux_cfg` namespaces)
   ↓
2. CommChannelsManager  (SPI, CAN, lazy UART — no I²C on Flux V1)
   ↓
3. GpioManager          (depends on comms for future expansion)
   ↓
4. ValveManager         (depends on comms + gpio for drivers)
   ↓
5. AdcManager           (ESP32 internal ADC, independent)
   ↓
6. LedManager           (WS2812, independent)
   ↓
7. TemperatureManager   (depends on ADC)
```

## Quick Start

### Basic Usage

```cpp
#include "api/Flux.h"
#include "core/hf-core-drivers/internal/hf-internal-interface-wrap/inc/base/BaseNvs.h"

// Get the Flux API instance
auto& flux = Flux::GetInstance();

// Ensure all systems are initialized
if (flux.EnsureInitialized()) {
    // Non-volatile storage (BaseNvs — calibration, config, app keys)
    BaseNvs& cal = flux.nvs.Calibration();
    (void)cal.KeyExists("my_cal_key");

    // Access communication channels
    auto& comms = flux.comms;

    // Access GPIO management
    auto& gpio = flux.gpio;

    // Access valve control
    auto& valves = flux.valves;

    // Access ADC channels
    auto& adc = flux.adc;

    // Access LED management
    auto& leds = flux.leds;

    // Access temperature sensors
    auto& temp = flux.temp;
}
```

### String-Based Access (Convenience)

```cpp
auto& flux = Flux::GetInstance();
if (flux.EnsureInitialized()) {
    // GPIO operations
    flux.gpio.Set("GPIO_MAX22200_FAULT", true);
    bool state;
    flux.gpio.Read("GPIO_TLE_DIAG", state);

    // ADC operations
    float voltage;
    flux.adc.ReadVoltage("ADC_NTC_THERM", voltage);

    // Valve control
    flux.valves.EnableChannel(ValveChannel::SOL_CH0);

    // LED status
    flux.leds.SetStatus(LedAnimation::STATUS_OK);
}
```

### Cached Access (High Performance)

```cpp
auto& flux = Flux::GetInstance();
if (flux.EnsureInitialized()) {
    // Cache pointers for fast access (do once, outside loops)
    auto* adc_ntc = flux.adc.Get("ADC_NTC_THERM");
    auto* adc_vbus = flux.adc.Get("ADC_VBUS_MONITOR");

    if (!adc_ntc || !adc_vbus) {
        printf("ERROR: Failed to cache component pointers\n");
        return;
    }

    // Use cached pointers in high-frequency operations
    for (int i = 0; i < 10000; i++) {
        float ntc_v, vbus_v;
        adc_ntc->ReadChannelV(0, ntc_v);
        adc_vbus->ReadChannelV(0, vbus_v);
    }
}
```

### Advanced Usage

```cpp
// Get system diagnostics
FluxSystemDiagnostics diagnostics;
if (flux.GetSystemDiagnostics(diagnostics)) {
    printf("System Health: %s\n",
           diagnostics.system_healthy ? "HEALTHY" : "UNHEALTHY");
    printf("Initialized Components: %u/%u\n",
           diagnostics.initialized_components, diagnostics.total_components);
}

// Collect per-manager health
ManagerHealthSnapshot snapshot;
if (flux.CollectManagerHealth(snapshot)) {
    for (size_t i = 0; i < snapshot.count; ++i) {
        printf("  %s: %s (err=%u)\n",
               snapshot.entries[i].name,
               snapshot.entries[i].has_error ? "ERROR" : "OK",
               snapshot.entries[i].last_error_code);
    }
}

// Perform health check
if (flux.PerformHealthCheck()) {
    printf("All systems operational\n");
}

// Dump comprehensive statistics
flux.DumpSystemStatistics();

// Get system information
printf("Version: %s\n", flux.GetSystemVersion());
printf("Uptime: %llu ms\n", flux.GetSystemUptimeMs());
printf("Init Time: %llu ms\n", flux.GetInitializationTimeMs());
```

## API Reference

### Singleton Access

```cpp
static Flux& GetInstance() noexcept;
```

Returns the singleton instance of the Flux API.

### Initialization

```cpp
bool EnsureInitialized() noexcept;
bool IsInitialized() const noexcept;
bool Shutdown() noexcept;
```

### Component Access

All components are accessed by reference:

```cpp
CommChannelsManager& comms;     // SPI, CAN, UART (no I²C on Flux V1)
GpioManager&         gpio;      // ESP32 GPIO pins
ValveManager&        valves;    // Solenoid + proportional valve control
AdcManager&          adc;       // ADC channels (NTC, VBUS)
LedManager&          leds;      // WS2812 status LED
TemperatureManager&  temp;      // Temperature sensors
```

### Diagnostics

```cpp
bool GetSystemDiagnostics(FluxSystemDiagnostics& diagnostics) const noexcept;
std::array<bool, kFluxManagerCount> GetComponentInitializationStatus() const noexcept;
size_t GetFailedComponents(const char* out[], size_t max_count) const noexcept;
size_t GetSystemWarnings(const char* out[], size_t max_count) const noexcept;
bool CollectManagerHealth(ManagerHealthSnapshot& snapshot) noexcept;
```

### Utility Methods

```cpp
uint64_t GetSystemUptimeMs() const noexcept;
uint64_t GetInitializationTimeMs() const noexcept;
void DumpSystemStatistics() const noexcept;
bool PerformHealthCheck() noexcept;
const char* GetSystemVersion() const noexcept;
```

### Convenience Accessor

```cpp
[[nodiscard]] inline Flux& GetFlux() noexcept;
```

Equivalent to `Flux::GetInstance()`.

### Macros

```cpp
#define FLUX_API        Flux::GetInstance()
#define FLUX_API_INIT() (Flux::GetInstance().EnsureInitialized(), Flux::GetInstance())
```

## System Diagnostics

### FluxSystemDiagnostics

```cpp
struct FluxSystemDiagnostics {
    bool system_healthy;
    bool nvs_initialized;
    bool comms_initialized;
    bool gpio_initialized;
    bool valves_initialized;
    bool adc_initialized;
    bool leds_initialized;
    bool temp_initialized;

    uint32_t total_components;  // equals kFluxManagerCount (7)
    uint32_t initialized_components;
    uint32_t failed_components;

    uint64_t initialization_time_ms;
    uint64_t system_uptime_ms;

    const char* failed_components_list[kFluxManagerCount];
    size_t failed_components_list_count;
};
```

### ManagerHealthSnapshot

```cpp
struct ManagerHealthSnapshot {
    ManagerHealthEntry entries[kFluxManagerCount];
    size_t count;
    size_t managers_with_errors;
    bool all_healthy;
};
```

## Performance

### Initialization Time

- **Fast Path**: ~30–80 ms (all components healthy)
- **Slow Path**: ~150–400 ms (with retries and error handling)

### Memory Usage

- **Static Memory**: ~1.5 KB (singleton instance)
- **Runtime Memory**: ~0.5 KB (diagnostics and state tracking)
- **Component Memory**: Varies by component (not owned by API)

### Thread Safety

- **Singleton Access**: Thread-safe (Meyer's singleton)
- **Initialization**: Thread-safe with atomic operations
- **Component Access**: Depends on individual component implementations

## NVS example (ESP32-S3)

See `examples/esp32/main/flux_nvs_example.cpp` — build and flash with:

`./scripts/build_app.sh flux_nvs_example Debug` then `./scripts/flash_app.sh flash_monitor flux_nvs_example Debug` from `examples/esp32/`.

## Related Documentation

- [Valve Manager](../../docs/component-handlers/VALVE_MANAGER_README.md)
- [ADC Manager](../../docs/component-handlers/ADC_MANAGER_README.md)
- [GPIO Manager](../../docs/component-handlers/GPIO_MANAGER_README.md)
- [LED Manager](../../docs/component-handlers/LED_MANAGER_README.md)
- [Temperature Manager](../../docs/component-handlers/TEMPERATURE_MANAGER_README.md)
- [Comm Channels Manager](../../docs/component-handlers/COMM_CHANNELS_MANAGER_README.md)
