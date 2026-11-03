# ValveManager

The reason the board exists. One API for fourteen valve channels on two different driver chips: eight on/off
solenoid channels on the MAX22200 and six proportional channels on the TLE92466ED. Source:
[`ValveManager.h`](../../lib/managers/ValveManager.h).

## Channels

| `ValveChannel` | Chip | Hardware channel | Control |
| --- | --- | --- | --- |
| `SOL_CH0` ... `SOL_CH7` | MAX22200 | 0-7 | On / off with a hit phase and a hold phase, set by current (CDR) or by duty (VDR) |
| `PROP_CH0` ... `PROP_CH5` | TLE92466ED | 0-5 | Regulated coil current in mA, with PWM frequency and dither |

The table is `kValveChannelMap` in the header; `FindChannel("SOL_CH3")` turns a name into a channel.

## API

| Call | Channels | Notes |
| --- | --- | --- |
| `EnableChannel(ch)`, `DisableChannel(ch)` | all | Switch one output |
| `EnableChannelsMask(uint16_t mask)` | all | Bits 0-7 solenoids, bits 8-13 proportional |
| `DisableAllChannels()` | all | |
| `IsChannelEnabled(ch)` | all | From the manager's channel state |
| `ConfigureSolenoidCdr(ch, hit_ma, hold_ma, hit_time_ms)` | solenoid | Current drive: hit current, hold current, hit time |
| `ConfigureSolenoidVdr(ch, hit_duty_pct, hold_duty_pct, hit_time_ms)` | solenoid | Voltage (duty) drive |
| `SetProportionalCurrent(ch, current_ma)` | proportional | Set-point of the current regulator |
| `ConfigureProportionalPwm(ch, frequency_hz)` | proportional | Must be > 0 |
| `ConfigureProportionalDither(ch, amplitude_ma, frequency_hz)` | proportional | Small oscillation that reduces valve stiction |
| `HasFault()`, `HasSolenoidFault()`, `HasProportionalFault()`, `ClearAllFaults()` | | |
| `GetSolenoidFaults(max22200::FaultStatus&)`, `GetProportionalFaults(tle92466ed::FaultReport&)` | | Chip-level fault detail |
| `GetChannelState(ch)` | all | `ValveChannelState`: enabled, configured, current, duty, PWM, dither, counters |
| `Max22200()`, `Tle92466()` | | Locked access to each chip, for features not wrapped here: `false` if that chip failed; the handler's locked calls and `WithDriver(fn)`, which runs `fn(driver)` under the handler mutex. There is no raw driver pointer |

All of them return `hf_valve_err_t` (or `bool`); calling a solenoid-only call on a proportional channel returns
`VALVE_ERR_CHANNEL_TYPE_MISMATCH`.

## Example: one solenoid and one proportional valve

```cpp
#include "api/Flux.h"
#include "esp_log.h"
#include "managers/ValveManager.h"

auto& flux = Flux::GetInstance();
if (flux.EnsureInitialized() && flux.valves.IsInitialized()) {
    auto& valves = flux.valves;

    // Solenoid: 800 mA for 50 ms to pull in, then hold at 200 mA.
    hf_valve_err_t err = valves.ConfigureSolenoidCdr(ValveChannel::SOL_CH0, 800, 200, 50.0f);
    if (HfValveErrOk(err)) {
        err = valves.EnableChannel(ValveChannel::SOL_CH0);
    }

    // Proportional: 200 Hz PWM, 400 mA set-point.
    (void)valves.ConfigureProportionalPwm(ValveChannel::PROP_CH0, 200.0f);
    if (HfValveErrOk(valves.EnableChannel(ValveChannel::PROP_CH0))) {
        (void)valves.SetProportionalCurrent(ValveChannel::PROP_CH0, 400);
    }

    if (!HfValveErrOk(err)) {
        ESP_LOGE("app", "valve: %s", HfValveErrToString(err));
    }
}
```

The [`valve_manager_example`](../examples/index.md) and `valve_cycling_test` apps go further.

## What start-up does

`EnsureInitialized()` brings up comms and GPIO, then tries each chip on its own, so the log names every missing
chip:

1. **TLE92466ED**: creates the handler, enters mission mode, drives its enable pin, clears the feedback freeze so
   the per-channel feedback registers update, and sets the supply under- / over-voltage trips to 19 V / 29 V for the
   24 V valve rail.
2. **MAX22200**: creates the handler with a 15 kΩ reference resistor (full-scale current 1000 mA), then reads
   STATUS back as a presence check: all ones, `ACTIVE=0` or a channel left on means nothing answered on the chip
   select (the MISO line is pulled up, and the driver alone accepts all ones as a live part).

It succeeds only if **both** chips work (fail-closed, [ADR 0005](../adr/0005-no-heap-fail-closed.md)). Otherwise
`IsInitialized()` stays `false`, every channel call returns `VALVE_ERR_NOT_INITIALIZED`, and `GetManagerError()`
says `HardwareFault` (a chip did not answer) or `BindFailed` (its SPI device or control pins are missing).
`IsBankReady(ValveDriverType::MAX22200 / TLE92466)` says which chip came up; that chip's handler accessor stays
non-null for diagnosis. Both handlers are members of the manager, constructed in place: no heap.

## Hardware enable path (SafeHold)

Both banks have an enable input that gates every output of the chip in hardware: MAX22200 `ENABLE` and TLE92466ED
`EN`. [ADR 0006](../adr/0006-enable-pin-hard-stop.md) records the decision.

| Call | What it does |
| --- | --- |
| `DropEnablePins()` | Drives both enables low through their GPIOs: no SPI, no mutex, no logging, works even if the manager failed to start. Latches until re-armed: `EnableChannel()` and `EnableChannelsMask()` then return `VALVE_ERR_WRONG_MODE`; disabling still works |
| `RearmEnablePins()` | Re-runs the valve bring-up (`Deinitialize()` then `EnsureInitialized()`), which raises both enables again. Uses SPI and the mutex |
| `IsOutputInhibited()`, `GetEnablePinStates(max, tle)` | Whether a drop is in force; whether each enable is driven high |
| `flux::hal::ValveBank::EnablePins(bool)` | The facade the product's safety thread calls ([`flux_hal_valve.hpp`](../../lib/api/flux_hal/flux_hal_valve.hpp)); returns `HalResult` |

The pins start low: `GpioManager` creates both enables driven low, and the `FAULT_N` lines of both chips as inputs,
before any valve code runs. The drivers' bring-up raises the enables. `Deinitialize()` (and `Flux::Shutdown()`) ends
with every channel off and both enables low. Nothing else in the HAL drops them: when to hold, and whether to vent a
side first, is the product's decision.

```cpp
// Safety thread, on a Stop-class latch: hardware first, then the SPI-level stop as today.
(void)flux::hal::ValveBank::EnablePins(false);
(void)Flux::GetInstance().valves.DisableAllChannels();
```

## Diagnostics

`GetSystemDiagnostics(ValveSystemDiagnostics&)`: which chips are up, configured and active channels, total
activations and errors. `GetChannelState(ch)` gives per-channel counters. `DumpStatistics()` logs both.

## Pitfalls

- **Configure, then enable.** A solenoid enabled before `ConfigureSolenoidCdr/Vdr` runs with whatever its
  channel registers hold.
- **Currents are limited by the chip setup.** The MAX22200 full scale is 1000 mA with this board's reference
  resistor; a set-point above it cannot be reached.
- **A dropped enable pin stays dropped.** Re-arm with `RearmEnablePins()` (or `ValveBank::EnablePins(true)`); an
  `EnsureInitialized()` on a running manager does not re-arm.
- **One chip missing means no valve manager.** A bench with only one driver fitted gets the other bank only through
  its handler, for diagnosis.
- **`GetLastError()`** holds the last failed call; it is not cleared by a later success.
- **Valve supply off** looks like a working chip on SPI with no coil current. Check the 24 V rail and
  `GetProportionalFaults()` (the TLE92466ED reports supply under-voltage).
