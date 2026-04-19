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

- **🔌 ESP32 ADC**: Up to 6 internal ADC channels
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
│  ESP32 ADC1 Peripheral │  ← Hardware layer
└──────────────────────────┘
```

## 📡 Hardware Sources

| Source | Channels | Use Case |
|--------|----------|----------|
| **ESP32 ADC1** | Up to 6 | VBUS monitoring, NTC thermistor readout |

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

## 🚨 Error Codes

### `hf_adc_err_t` Error Code Reference

| Category | Codes | Description |
|----------|-------|-------------|
| **General** | `ADC_SUCCESS` (0) | Operation completed successfully |
| | `ADC_ERR_FAILURE` (1) | General failure |
| | `ADC_ERR_NOT_INITIALIZED` (2) | Not initialized |
| **Parameter** | `ADC_ERR_INVALID_PARAMETER` (4) | Invalid parameter |
| | `ADC_ERR_NULL_POINTER` (5) | Null pointer |
| **Channel** | `ADC_ERR_CHANNEL_NOT_FOUND` (7) | Channel not found |
| | `ADC_ERR_CHANNEL_NOT_ENABLED` (8) | Channel not enabled |
| | `ADC_ERR_CHANNEL_READ_ERR` (11) | Channel read error |
| | `ADC_ERR_INVALID_CHANNEL` (13) | Invalid channel |
| **Sampling** | `ADC_ERR_SAMPLE_TIMEOUT` (16) | Sample timeout |
| | `ADC_ERR_VOLTAGE_OUT_OF_RANGE` (23) | Voltage out of range |
| **Hardware** | `ADC_ERR_HARDWARE_FAULT` (19) | Hardware fault |
| | `ADC_ERR_COMMUNICATION_FAILURE` (20) | Communication failure |
| **Calibration** | `ADC_ERR_CALIBRATION_FAILURE` (22) | Calibration failure |
| | `ADC_ERR_CALIBRATION_NOT_FOUND` (28) | Calibration data not found |
| **System** | `ADC_ERR_SYSTEM_ERROR` (40) | System error |
| | `ADC_ERR_TIMEOUT` (45) | Operation timeout |
| | `ADC_ERR_UNKNOWN` (57) | Unknown error |

> Full enum: see `hf_adc_err_t` in `BaseAdc.h` (58 values covering channel, calibration, DMA, and system errors).

## ⚠️ Notes

- All channels are registered at `EnsureInitialized()` time based on board configuration
- The manager does **not** own the ADC driver memory via smart pointers — it uses opaque `void*` storage allocated once at init (singleton lifetime)
- `ReadVoltage()` applies the calibration reference configured per channel
