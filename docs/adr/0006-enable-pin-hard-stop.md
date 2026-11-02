# ADR 0006: A hardware enable path for both valve banks, with the hold policy left to the product

| | |
| --- | --- |
| Status | Accepted |
| Date | 2026-11-02 |

## Context

Today "safe" means every valve is commanded to 0 over SPI. Nothing de-energises a bank independently of SPI: a
stalled transfer, a hung valve-bus mutex or a stuck control thread leaves every output as it was. Both driver ICs
have an enable input (MAX22200 `ENABLE` on GPIO2, TLE92466ED `EN` on GPIO5) that gates the whole bank in hardware,
but only the drivers' own bring-up touched them. Two smaller defects sat next to it: the pins started as plain
outputs with whatever level the MCU left them at, and the `FAULT_N` lines of both chips were created as outputs and
driven until `ValveManager` reconfigured them.

The product's port design (architecture-port §3.4, §3.7, §3.8) gives the safety thread a SafeHold that ends with
both enable pins low, reachable without SPI. Whether a side is vented before the hold (vent-then-hold) and whether
any vent valve is normally open are open owner questions (Q1, Q2); the first port step keeps today's hold.

## Options

1. **Drop the pins inside `ValveManager` on any fault it sees.** Puts safety policy in the HAL and changes
   behaviour today.
2. **Expose the hardware path, do not call it on the HAL's own initiative except at shutdown.** The product's
   arbiter decides when.

## Decision

Option 2.

- `ValveManager::DropEnablePins()` drives both enables low directly through their GPIOs: no SPI, no mutex, no
  allocation, no logging, whether or not the manager is initialised. It latches: channel-energising calls are then
  refused with `VALVE_ERR_WRONG_MODE` until `RearmEnablePins()`, which re-runs the valve bring-up (the MAX22200 is
  not relied on to keep its configuration across an ENABLE low period). `flux::hal::ValveBank::EnablePins(bool)` is
  the facade the product calls.
- `GpioManager` creates the two `FAULT_N` lines as inputs and the two enables driven low from the first instruction
  (`kInputPins`, `kValveEnablePins` in `bsp_bind/board/flux_v1`). The banks stay off until the valve drivers' own
  bring-up raises the enables, exactly as before.
- `ValveManager::Deinitialize()` (and so `Flux::Shutdown()`) ends in SafeHold: every channel off over SPI, then both
  enables low.
- Nothing else drops the pins. Normal operation, `DisableAllChannels()` and fault handling behave as before.

## Consequences

- The product's safety thread can de-energise both banks in one GPIO write each, without depending on the valve bus
  or its owner thread (design B2: pins low within 6 ms of a latch).
- External pull-downs on `ENABLE`, `EN` and `RESN` (owner question Q3) are still needed for reset, brown-out and the
  boot ROM; software cannot hold a pin it does not drive yet.
- Vent-then-hold, if the owner chooses it (Q2), is a sequence in the product (relief valves energised for a bounded
  time, then `EnablePins(false)`); it needs no HAL change.
