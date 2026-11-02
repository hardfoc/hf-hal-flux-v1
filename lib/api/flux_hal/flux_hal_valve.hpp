/**
 * @file flux_hal_valve.hpp
 * @brief ValveBank facade, first slice: the hardware enable path of both valve banks.
 *
 * ## Purpose
 * The safe-state entry point the product's safety thread calls (architecture-port design §3.4
 * "Hardware enable path", §3.7, §3.8). `EnablePins(false)` drives MAX22200 ENABLE and TLE92466ED
 * EN low: every solenoid and proportional output is de-energised in hardware, without SPI and
 * without taking the valve manager's mutex. It is the hardware half of SafeHold; the software
 * half (every channel commanded to 0 over SPI) stays with the caller, exactly as today.
 *
 * This first slice does not decide policy: when to drop the pins, and whether a side is vented
 * before the hold, belong to the product's safety arbiter (owner questions Q1, Q2).
 *
 * ## Ownership
 * Stateless wrapper over `ValveManager` (DropEnablePins / RearmEnablePins). The rest of the
 * ValveBank facade (set-points, readback) arrives with the composition root.
 *
 * Thread safety: `EnablePins(false)` is lock-free and SPI-free (see ValveManager::DropEnablePins).
 * `EnablePins(true)` re-runs valve bring-up: mutex, SPI, logging; never from the safety path.
 *
 * Error handling: facade lane, @ref flux::hal::HalResult.
 *
 * @author HardFOC Team
 * @date 2026
 */

#ifndef FLUX_HAL_VALVE_HPP_
#define FLUX_HAL_VALVE_HPP_

#include "flux_hal/flux_hal_types.hpp"

namespace flux::hal {

/** @brief Both valve banks, as one hardware-gated actuator group. */
struct ValveBank {
    /**
     * @brief Drop (false) or re-arm (true) both valve-bank enable pins.
     * @param on false: drive both enables low now, latch the drop, refuse channel energising calls.
     *           true: re-arm by re-running the valve bring-up, which raises both pins.
     * @return Ok, or Err(HardwareFault) when a pin could not be driven / bring-up failed,
     *         Err(NotInitialized) when no enable pin exists (GPIO manager down) on a drop.
     */
    static HalResult EnablePins(bool on) noexcept;

    /** @brief Both enable pins are driven high (banks may be energised by channel commands). */
    [[nodiscard]] static bool EnablePinsHigh() noexcept;

    /** @brief The pins were dropped and not re-armed since. */
    [[nodiscard]] static bool IsHeldOff() noexcept;
};

}  // namespace flux::hal

#endif  // FLUX_HAL_VALVE_HPP_
