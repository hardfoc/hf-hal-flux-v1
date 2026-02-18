# AdcManager - ADC Channel Management System

<div align="center">

![Component](https://img.shields.io/badge/component-AdcManager-blue.svg)
![Thread Safe](https://img.shields.io/badge/thread--safe-yes-green.svg)
![Hardware](https://img.shields.io/badge/hardware-ESP32--C6-orange.svg)

**Unified ADC channel management for the HardFOC Flux platform**

</div>

## 📋 Overview

The `AdcManager` is a singleton component handler that provides unified access to all ADC channels on the Flux board. It abstracts hardware-specific ADC drivers behind a consistent string-based API.

### Key Features

- **🔌 ESP32-C6 ADC**: Up to 6 internal ADC channels
- **📊 Voltage Conversion**: Automatic raw-to-voltage calibration
- **🏷️ String-Based API**: Access channels by name (`"ADC_VBUS"`) or functional enum
- **🔒 Thread-Safe**: Concurrent access from multiple RTOS tasks
- **📈 Diagnostics**: Per-channel statistics and health monitoring

## 🏗️ Architecture

```
┌──────────────────────────┐
│        AdcManager         │  ← Singleton component handler
├──────────────────────────┤
│  std::array<AdcEntry>     │  ← Fixed-size channel registry
│  EspAdc drivers (stack)   │  ← Hardware driver instances
├──────────────────────────┤
│  ESP32-C6 ADC1 Peripheral │  ← Hardware layer
└──────────────────────────┘
```

## 📡 Hardware Sources

| Source | Channels | Use Case |
|--------|----------|----------|
| **ESP32-C6 ADC1** | Up to 6 | VBUS monitoring, NTC thermistor readout |

## 🔧 API Reference

### Initialization

```cpp
auto& adc = AdcManager::GetInstance();
bool ok = adc.EnsureInitialized();  // Returns true on success
```

### Reading Channels

```cpp
// By name
float voltage = adc.ReadVoltage("ADC_VBUS");
int raw = adc.ReadRaw("ADC_NTC");

// By functional enum
float v = adc.ReadVoltage(HfFunctionalAdcChannel::kVbus);
```

### Diagnostics

```cpp
auto diag = adc.GetSystemDiagnostics();
adc.DumpStatistics();  // Prints all channel stats to logger
```

## ⚠️ Notes

- All channels are registered at `EnsureInitialized()` time based on board configuration
- The manager does **not** own the ADC driver memory via smart pointers — it uses opaque `void*` storage allocated once at init (singleton lifetime)
- `ReadVoltage()` applies the calibration reference configured per channel
