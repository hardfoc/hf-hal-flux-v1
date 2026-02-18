# GpioManager - GPIO Pin Management System

<div align="center">

![Component](https://img.shields.io/badge/component-GpioManager-blue.svg)
![Thread Safe](https://img.shields.io/badge/thread--safe-yes-green.svg)
![Hardware](https://img.shields.io/badge/hardware-ESP32--C6-orange.svg)

**Unified GPIO pin management for the HardFOC Flux platform**

</div>

## 📋 Overview

The `GpioManager` is a singleton component handler that provides unified access to all GPIO pins on the Flux board. Pins are accessed by string name or functional enum, with the hardware driver (`EspGpio`) stored by value in a fixed-size array — no heap allocation.

### Key Features

- **🔌 ESP32-C6 GPIO**: Direct pin control for all available GPIO
- **🏷️ String-Based API**: Access pins by name (`"GPIO_LED_STATUS"`) or functional enum
- **📦 Stack-Allocated**: `EspGpio` drivers stored by value in `std::array`
- **🔒 Thread-Safe**: Safe for concurrent RTOS task access
- **⚡ Direction & Pull Config**: Input/output, pull-up/pull-down, open-drain

## 🏗️ Architecture

```
┌──────────────────────────┐
│       GpioManager         │  ← Singleton
├──────────────────────────┤
│  std::array<GpioEntry>    │  ← Fixed-size pin registry
│    EspGpio driver (value) │  ← Stack-allocated, no heap
├──────────────────────────┤
│  ESP32-C6 GPIO Peripheral │
└──────────────────────────┘
```

## 🔧 API Reference

### Initialization

```cpp
auto& gpio = GpioManager::GetInstance();
bool ok = gpio.EnsureInitialized();
```

### Pin Control

```cpp
// Set/clear
gpio.Set("GPIO_VALVE_EN", true);
gpio.SetActive("GPIO_LED_STATUS");
gpio.SetInactive("GPIO_LED_STATUS");

// Read
bool state = gpio.Read("GPIO_FAULT_N");
bool active = gpio.IsActive("GPIO_FAULT_N");

// Toggle
gpio.Toggle("GPIO_LED_STATUS");
```

### Configuration

```cpp
gpio.SetDirection("GPIO_PROBE", GpioDirection::kInput);
gpio.SetPullMode("GPIO_PROBE", GpioPullMode::kPullUp);
gpio.SetOutputMode("GPIO_VALVE_EN", GpioOutputMode::kPushPull);
```

### Functional Enum Access

```cpp
gpio.Set(HfFunctionalGpioPin::kValveEnable, true);
bool fault = gpio.Read(HfFunctionalGpioPin::kFaultN);
```

## ⚠️ Notes

- All `GpioEntry` structs including their `EspGpio` driver are stack-allocated — zero heap usage
- Pin names and mappings are defined at compile time in the board configuration
- The manager does not support interrupt callbacks (unlike Vortex's multi-source GPIO system)
