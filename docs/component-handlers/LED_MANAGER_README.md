# LedManager - LED Control System

<div align="center">

![Component](https://img.shields.io/badge/component-LedManager-blue.svg)
![Thread Safe](https://img.shields.io/badge/thread--safe-yes-green.svg)
![Hardware](https://img.shields.io/badge/hardware-WS2812-orange.svg)

**WS2812 LED strip control and animation for the HardFOC Flux platform**

</div>

## 📋 Overview

The `LedManager` is a singleton that manages a WS2812 addressable LED strip on GPIO3. It provides color control, built-in animations, and status indicator patterns through the `WS2812Strip` and `WS2812Animator` drivers.

### Key Features

- **💡 WS2812 RGB**: Full RGB control via RMT interface
- **🎨 Color Modes**: RGB, HSV, and temperature-based color control
- **🎬 Animations**: Built-in animations (breathe, rainbow, pulse, chase, etc.)
- **📊 Status Indicators**: Pre-defined patterns for system health, warnings, errors
- **🔋 Power Management**: Current limiting and brightness control
- **🔒 Thread-Safe**: Concurrent access via RTOS mutex

## 🏗️ Architecture

```
┌──────────────────────────────┐
│         LedManager            │  ← Singleton
├──────────────────────────────┤
│  unique_ptr<WS2812Strip>      │  ← LED strip driver
│  unique_ptr<WS2812Animator>   │  ← Animation engine
├──────────────────────────────┤
│  ESP32-C6 RMT → GPIO3        │  ← Hardware
└──────────────────────────────┘
```

## 🔧 API Reference

### Initialization

```cpp
auto& led = LedManager::GetInstance();
bool ok = led.EnsureInitialized();
```

### Color Control

```cpp
// Set solid color (RGB)
led.SetColor(LedColor(255, 0, 0));  // Red

// Set by HSV
led.SetHSV(120, 255, 200);  // Green

// Set individual pixel
led.SetPixelColor(0, LedColor(0, 0, 255));

// Brightness
led.SetBrightness(128);  // 50%
```

### Animations

```cpp
led.StartAnimation(LedAnimation::kBreathing);
led.StartAnimation(LedAnimation::kRainbow);
led.StartAnimation(LedAnimation::kPulse);
led.StopAnimation();
```

### Status Indicators

```cpp
led.ShowStatus(LedStatus::kOk);       // Green steady
led.ShowStatus(LedStatus::kWarning);  // Yellow breathe
led.ShowStatus(LedStatus::kError);    // Red pulse
led.ShowStatus(LedStatus::kBusy);     // Blue chase
```

### Shutdown

```cpp
led.Shutdown();  // Turns off all LEDs and releases animation resources
```

## ⚠️ Notes

- Uses `std::unique_ptr` for both `WS2812Strip` and `WS2812Animator` — proper RAII ownership
- `Shutdown()` serves as the de-initialization path
- GPIO pin is resolved dynamically via `GetGpioMapping()` at init time
- RMT channel is allocated from the ESP32-C6 RMT peripheral pool
