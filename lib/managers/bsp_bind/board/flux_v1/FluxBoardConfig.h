/**
 * @file FluxBoardConfig.h
 * @brief Flux V1 board wiring that is not in the pin map: bus controllers, clocks, modes, bit rates.
 *
 * @details The functional pin map (hf-pincfg `hf_functional_pin_config_flux_v1.hpp`) says which GPIO
 *          carries which signal. This header holds the rest of the board's bus facts, as plain
 *          constants with no MCU header: which SPI controller each bus uses, the clock and mode of
 *          each chip select, the CAN bit rate and the status LED. `CommChannelsManager` and
 *          `LedManager` turn them into `flux::bsp` specs. A board revision changes this file and the
 *          pin map, not the managers.
 *
 * Ownership: constants only.
 * Thread safety: constexpr; no state.
 *
 * @author HardFOC Team
 * @date 2026
 */

#ifndef FLUX_BOARD_CONFIG_H_
#define FLUX_BOARD_CONFIG_H_

#include <cstdint>

#include "core/hf-core-drivers/internal/hf-pincfg/src/hf_functional_pin_config_flux_v1.hpp"

namespace flux::board {

//==============================================================================
// SPI buses
//==============================================================================

/** @brief Logical SPI buses on Flux V1 (binding slot index). */
enum class SpiBus : uint8_t {
    Valve    = 0,  ///< MAX22200 + TLE92466ED (pin map: SPI2_*)
    Pressure = 1,  ///< ADS7952 pressure ADC, dedicated (pin map: SPI3_*)
};

/* Valve bus: GPIO35..38 have no IO MUX route, so it runs on SPI3_HOST through the GPIO matrix. */
inline constexpr uint8_t  kValveSpiController    = 2;      ///< ESP32-S3 SPI3_HOST
inline constexpr bool     kValveSpiUseIomux      = false;  ///< GPIO matrix

/* Pressure bus: GPIO10..13 are the native IO MUX pins of SPI2_HOST. */
inline constexpr uint8_t  kPressureSpiController = 1;      ///< ESP32-S3 SPI2_HOST
inline constexpr bool     kPressureSpiUseIomux   = true;   ///< IO MUX

inline constexpr uint32_t kSpiTimeoutMs          = 1000;   ///< Per-transfer timeout, both buses

/* MAX22200: mode 0, 1 MHz. The datasheet allows 10 MHz; 1 MHz is the rate the driver example
 * enumerates reliably on the bench harness. */
inline constexpr uint32_t kMax22200SpiHz   = 1'000'000;
inline constexpr uint8_t  kMax22200SpiMode = 0;

/* TLE92466ED: mode 1, 1 MHz (datasheet allows 8 MHz with a tightly controlled bus). */
inline constexpr uint32_t kTle92466SpiHz   = 1'000'000;
inline constexpr uint8_t  kTle92466SpiMode = 1;

/* ADS7952: mode 0, 4 MHz (datasheet allows 20 MHz; 4 MHz for stability). */
inline constexpr uint32_t kAds7952SpiHz    = 4'000'000;
inline constexpr uint8_t  kAds7952SpiMode  = 0;

//==============================================================================
// CAN, status LED
//==============================================================================

inline constexpr uint32_t kCanBitrate          = 500'000;  ///< TWAI nominal bit rate (bit/s)

inline constexpr uint8_t  kStatusLedChannel    = 0;        ///< RMT channel of the WS2812 data line
inline constexpr uint16_t kStatusLedCount      = 1;        ///< LEDs on the strip

//==============================================================================
// GPIO roles the pin map does not carry (it has no direction field)
//==============================================================================

/**
 * @brief Pins that are inputs on Flux V1: the open-drain fault outputs of the two valve drivers.
 * @details GpioManager creates them as inputs from the first instruction, so the MCU never drives
 *          a chip's FAULT_N line, not even for the moment between pin creation and the valve
 *          manager's bring-up.
 */
inline constexpr HfFunctionalGpioPin kInputPins[] = {
    HfFunctionalGpioPin::MAX22200_FAULT_N,
    HfFunctionalGpioPin::TLE92466_FAULT_N,
};

/**
 * @brief The valve-bank enable outputs (MAX22200 ENABLE, TLE92466ED EN), active high.
 * @details Driving either low de-energises its whole bank in hardware, without SPI. GpioManager
 *          creates them driven inactive (low), so both banks are off until the valve manager's
 *          bring-up raises them; `ValveManager::DropEnablePins()` drives them low again.
 */
inline constexpr HfFunctionalGpioPin kValveEnablePins[] = {
    HfFunctionalGpioPin::MAX22200_ENABLE,
    HfFunctionalGpioPin::TLE92466_EN,
};

/** @brief True when @p pin is in @ref kInputPins. */
constexpr bool IsInputPin(HfFunctionalGpioPin pin) noexcept {
    for (const auto p : kInputPins) {
        if (p == pin) return true;
    }
    return false;
}

/** @brief True when @p pin is in @ref kValveEnablePins. */
constexpr bool IsValveEnablePin(HfFunctionalGpioPin pin) noexcept {
    for (const auto p : kValveEnablePins) {
        if (p == pin) return true;
    }
    return false;
}

}  // namespace flux::board

#endif  // FLUX_BOARD_CONFIG_H_
