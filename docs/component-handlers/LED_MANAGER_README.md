# LedManager - LED Control System

<div align="center">

![Component](https://img.shields.io/badge/component-LedManager-blue.svg)
![Thread Safe](https://img.shields.io/badge/thread--safe-yes-green.svg)
![Hardware](https://img.shields.io/badge/hardware-WS2812-orange.svg)

**WS2812 LED control and animation for the HardFOC Flux V1 platform**

</div>

## Overview

The `LedManager` is a singleton that drives a single WS2812 addressable RGB LED on GPIO3 via the ESP-IDF RMT peripheral. It provides solid-colour display, built-in animations, brightness control, and convenience status-indicator methods.

### Key Features

- **WS2812 RGB**: Full 24-bit colour control via ESP-IDF RMT
- **Animations**: Solid, blink, breathe, and rainbow modes
- **Status Indicators**: Boot, ready, warning, error, and calibration patterns
- **Brightness Control**: Percent-based and raw (0–255) with configurable ceiling
- **Thread-Safe**: All operations protected by RtosMutex
- **Diagnostics**: Operation counters, animation state, and health snapshot

## Architecture

```
┌──────────────────────────────┐
│         LedManager           │  ← Meyers singleton
├──────────────────────────────┤
│  unique_ptr<WS2812Strip>     │  ← RMT LED strip driver
│  unique_ptr<WS2812Animator>  │  ← Animation engine
├──────────────────────────────┤
│  ESP32-S3 RMT → GPIO3       │  ← Hardware
└──────────────────────────────┘
```

## Quick Start

```cpp
#include "managers/LedManager.h"

auto& leds = LedManager::GetInstance();
leds.EnsureInitialized();

// Set solid red
leds.SetColor(LedColors::RED);

// Start rainbow animation
leds.StartAnimation(LedAnimation::RAINBOW);
// Drive animation in a loop:
leds.UpdateAnimation();  // Call every ~50 ms
```

## API Reference

### Singleton & Lifecycle

| Method | Description |
|--------|-------------|
| `GetInstance()` | Access the singleton (thread-safe Meyers singleton). |
| `EnsureInitialized()` | Initialise WS2812 strip + animator (idempotent). Returns `true` on success. |
| `Shutdown()` / `Deinitialize()` | Turn off LED and release driver resources. |
| `IsInitialized()` | Check whether the driver is ready. |

### Colour Control

```cpp
// LedColor struct
LedError SetColor(const LedColor& color, uint32_t led_index = 0);
// Packed 0xRRGGBB
LedError SetColor(uint32_t rgb, uint32_t led_index = 0);
// Read back
LedError GetCurrentColor(LedColor& color) const;
// Turn off
LedError TurnOff();
```

### Brightness

```cpp
LedError SetBrightnessPercent(uint8_t percent);            // [0, 100]
LedError SetBrightnessRaw(uint8_t raw);                    // [0, 255]
LedError SetBrightness(uint8_t raw);                       // Alias for SetBrightnessRaw
LedError SetMaxBrightness(uint8_t max);                    // Set ceiling [0, 255]
LedError GetCurrentBrightnessPercent(uint8_t& pct) const;  // Read back as %
LedError GetCurrentBrightnessRaw(uint8_t& raw) const;      // Read back as raw
```

### Animations

```cpp
LedError StartAnimation(LedAnimation animation, const LedColor& color = LedColors::WHITE);
LedError StopAnimation();
bool     IsAnimationActive() const;
LedError UpdateAnimation();  // Call periodically (~50 ms)
```

**Available animations:** `OFF`, `SOLID`, `BLINK`, `BREATH`, `RAINBOW`, `STATUS_OK`, `STATUS_WARN`, `STATUS_ERROR`, `STATUS_BOOT`, `STATUS_CALIBRATE`.

### Status Indication

```cpp
LedError SetStatus(LedAnimation animation);  // Generic status setter
LedError IndicateBoot();        // Blue blink
LedError IndicateReady();       // Solid green
LedError IndicateWarning();     // Yellow blink
LedError IndicateError();       // Red fast-blink
LedError IndicateCalibration(); // Cyan breathe
```

### Utility

```cpp
static uint32_t ColorWheel(uint8_t position);  // Rainbow colour from 0–255
void DumpStatistics() const;                    // Log state to console
gpio_num_t GetCurrentGpioPin() const;
uint32_t   GetLedCount() const;                 // Always 1 on Flux V1
```

### Diagnostics

```cpp
LedError GetSystemDiagnostics(LedSystemDiagnostics& diag) const;
LedError GetLastError() const;
```

`LedSystemDiagnostics` contains: `system_healthy`, `led_initialized`, `animation_active`, `current_animation`, `total_operations`, `successful_operations`, `failed_operations`, `animation_cycles`, `current_brightness`, `current_color`.

### Convenience

```cpp
LedManager& GetLedManager();  // Free-function alias for GetInstance()
```

## Error Codes

| Code | Description |
|------|-------------|
| `SUCCESS` | Operation completed successfully. |
| `NOT_INITIALIZED` | Manager not yet initialised. |
| `INITIALIZATION_FAILED` | WS2812/RMT setup failed. |
| `INVALID_PARAMETER` | Argument out of range. |
| `HARDWARE_ERROR` | RMT peripheral communication error. |
| `ANIMATION_FAILED` | Animation start/update failed. |
| `INVALID_COLOR` | Colour value rejected. |
| `INVALID_BRIGHTNESS` | Brightness out of [0, max] range. |

## Colour Constants

The `LedColors` namespace provides: `BLACK`, `RED`, `GREEN`, `BLUE`, `YELLOW`, `CYAN`, `MAGENTA`, `WHITE`, `ORANGE`, `PURPLE`.

## Hardware Notes

- **LED**: Single WS2812 NeoPixel on GPIO3
- **Interface**: ESP32-S3 RMT peripheral (channel 0)
- **Protocol**: WS2812B, 800 kHz, 24-bit RGB
- **Animation tick**: Call `UpdateAnimation()` every ~50 ms from a FreeRTOS task

## See Also

- [GpioManager Documentation](GPIO_MANAGER_README.md)
- [LedManagerExample.cpp](../../examples/LedManagerExample.cpp)

---

*Part of the HardFOC Flux V1 HAL. See [Documentation Index](../../DOCUMENTATION_INDEX.md).*
