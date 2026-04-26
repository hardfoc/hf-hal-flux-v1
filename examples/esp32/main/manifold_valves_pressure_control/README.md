# Manifold valves & pressure sensor application

This example is the WH bench-test acquisition + actuation stack. It owns:

- **8 Honeywell SSC TruStability** ratiometric pressure transducers on the
  ADS7952 12-bit SPI ADC.
- **7 Parker C15 / C21 / SRS / VSO** valves driven through the **MAX22200**
  octal solenoid driver (6 binary hit/hold + 1 pseudo-proportional).
- **6 Parker VSO / VSO MaxFlow / LM-Pro** precision proportional valves
  driven through the **TLE92466ED** 6-channel current-controlled driver.

A boot-time **valve self-test** exercises every catalog entry one at a time
(solenoids ON/OFF, proportionals 0 → full → 0) and reports per-channel
faults from the underlying drivers — see §6.

Everything hangs off the existing Flux V1 HAL — this folder contributes the
**application-level abstractions** (named valves, datasheet-driven
profiles, lock-free positional API, dedicated control + diagnostics
threads, full per-driver fault decoding).

---

## 1. Architecture overview

```
┌────────────────────────────────────────────────────────────────────┐
│  Application / control-loop threads (any rate)                     │
│   - PressureSensor::GetPressureFiltered()      lock-free atomic    │
│   - ManifoldValves::Get(SolenoidId).RequestState/RequestPosition    │
│   - ManifoldValves::Get(ProportionalId).RequestPosition             │
│   - .GetActualPosition() / .GetState() / .Snapshot()  lock-free    │
└────────────────────────────────────────────────────────────────────┘
          │                                         │
          ▼                                         ▼
┌──────────────────────────────┐   ┌──────────────────────────────┐
│ PressureSensorThread (1 kHz) │   │ ManifoldValvesControlThread (100 Hz)│
│  - Auto-1 batched ADS7952    │   │  - ConsumeDirty() per valve  │
│  - Per-channel single-pole   │   │  - Apply() → ValveManager    │
│    IIR + atomic publish      │   │  - Updates actual_position   │
└──────────────────────────────┘   └──────────────────────────────┘
                              │
                              ▼
┌────────────────────────────────────────────────────────────────────┐
│ ManifoldValvesDiagnosticsThread (10 Hz)                                        │
│  - Per-valve OnDiagnostics() → per-channel snapshot                │
│  - MAX22200 STATUS register   → SubsystemFaultSnapshot.max_*       │
│  - TLE92466 FaultReport       → SubsystemFaultSnapshot.tle_*       │
│  - Aggregates lock-free SubsystemFaultSnapshot for the app         │
└────────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌────────────────────────────────────────────────────────────────────┐
│ Flux::ValveManager  +  Flux::AdcManager  +  Flux::CommChannels     │
│  - MAX22200 (SOL_CH0..SOL_CH5)   ← all 6 solenoids                 │
│  - TLE92466ED (PROP_CH0..PROP_CH5) ← all 6 precision proportionals │
│  - ADS7952 (SPI3)                  ← pressure ADC                  │
└────────────────────────────────────────────────────────────────────┘
```

### 1.1 Why three threads?

| Thread | Period | Job |
|--------|--------|-----|
| Pressure sampling | 1 ms | Match the SSC ASIC's 1 kHz pressure update; produce filtered atomic snapshots that any control loop can consume lock-free. |
| Valve control | 10 ms | Push every dirty valve setpoint to MAX22200 / TLE92466ED. Doing this at 100 Hz keeps SPI traffic low while still feeling instant to a 500 Hz outer control loop. |
| Diagnostics | 100 ms | Read per-channel + global driver-IC fault registers and surface a comprehensive `SubsystemFaultSnapshot` so the application doesn't ever have to do multi-register SPI reads itself. |

This separation means the control loop **never** has to read fault
registers itself — that's the diagnostics thread's responsibility — and
the diagnostics thread **never** delays the application's set/get path.

---

## 2. Pressure sensors

Honeywell SSC TruStability sensors output **10 % – 90 % of V_supply** over
the configured pressure range (transfer function "A"), with output clipping
limits at 2.5 % / 97.5 %. Resolution ≥ 0.03 % FSS, accuracy ±0.25 % FSS
BFSL, total error band ±2 % FSS, ASIC update ≈ 1 kHz, response ≈ 1 ms.

Bench wiring:

| Channel | Label    | Range            | Direction  |
|---------|----------|------------------|------------|
| CH0     | DPS_LOW  | ±0.18 PSI        | Differential |
| CH1     | DPS_HIGH | ±2.32 PSI        | Differential |
| CH2     | PMS      | 0 – 5 PSI        | Positive |
| CH3     | PLS_LOW  | 0 – 15 PSI       | Positive |
| CH4     | PLS_HIGH | 0 – 150 PSI      | Positive |
| CH5     | RLS      | 0 → -30 PSI      | Negative (vacuum) |
| CH6     | PWS2     | 0 – 1 PSI        | Positive |
| CH7     | PWS1     | 0 – 1 PSI        | Positive |

Pipeline: 1 kHz Auto-1 batched ADS7952 read → per-channel single-pole IIR
LPF (default fc = 100 Hz at fs = 1 kHz) → atomic publish. Lock-free
`PressureSensor::GetPressureFiltered()` for control loops.

---

## 3. Valves

### 3.1 Driver-IC capabilities (what we expose)

#### MAX22200 (octal solenoid driver, 8 channels — using 6)

| Capability | Use here |
|------------|----------|
| **CDR (Current Drive Regulation)** — 7-bit current ladder per channel | Hit & hold currents on every BinaryHitHold solenoid; sustained current on the SEV pseudo-proportional. |
| **VDR (Voltage / PWM Duty Regulation)** | Available via `ValveManager::ConfigureSolenoidVdr` — not used today. |
| **Per-channel feature gates** (CFG_CH register) | All four exposed via `Max22200ChannelFeatures`: `slew_rate_control` (cleaner edges, lower EMI), `open_load_detection`, `plunger_movement_detection`, `hit_current_check`. |
| **DPM (Detection of Plunger Movement)** — global algorithm (CFG_DPM) | `ManifoldValves::ConfigureGlobalDpm(start_ma, dip_ma, debounce_ms)` configures ISTART / IPTH / TDEB. Per-channel DPM_EN bits set from the catalog's feature gate. |
| **Per-channel faults** (FAULT register) | `SolenoidValve::Snapshot()` returns `overcurrent`, `hit_not_reached`, `open_load`, `plunger_movement_fault` plus sticky counters. |
| **Global health** (STATUS register) | `ManifoldValvesDiagnosticsThread::SubsystemFaultSnapshot.max_*` carries OVT, COMER, UVM, the global OCP/OLF/HHF/DPM bits and the active-channels mask. |

#### TLE92466ED (six-channel current-controlled, 6 channels — using all 6)

| Capability | Use here |
|------------|----------|
| **Closed-loop current control** | `ValveManager::SetProportionalCurrent` per valve, called every dirty Apply. |
| **PWM frequency** (mantissa+exponent encoded) | Each valve's `pwm_hz` from the model spec is pushed once via `ValveManager::ConfigureProportionalPwm`. |
| **Dither** (step size + steps + flat period) | Pushed once via `ValveManager::ConfigureProportionalDither` for VSO-class valves. LM-Pro skips dither (it's a linear motor). |
| **Slew rate / diagnostic current / open-load threshold / OLSG warning** | Configured via `tle92466ed::ChannelConfig` from `Tle92466ChannelFeatures`. |
| **Per-channel diagnostics** | `ProportionalValve::Snapshot()` carries every flag from `tle92466ed::ChannelDiagnostics`: `overcurrent`, `short_to_ground`, `open_load`, `over_temperature`, `open_load_short_ground`, `ot_warning`, `current_regulation_warning`, `pwm_regulation_warning`, `olsg_warning`, plus live measurements `average_current_raw`, `duty_cycle_raw`, `vbat_feedback_raw`. |
| **Global health** (FaultReport) | `ManifoldValvesDiagnosticsThread::SubsystemFaultSnapshot.tle_*` carries VBAT/VIO/VDD UV/OV, central OT warning/error, clock fault, SPI watchdog, POR/reset events, register/OTP ECC errors, supply-NOK summaries. |

### 3.2 Bench mapping

**Solenoids (MAX22200, channels 0–7):**

| OUTn       | ID    | Drive mode          | Model                         | Datasheet currents (24 V)            |
|------------|-------|---------------------|-------------------------------|---------------------------------------|
| SOL_CH0    | _spare_ | _unconnected_       | —                              | —                                    |
| SOL_CH1    | OLV_2 | BinaryHitHold       | Parker C21 (R = 235 Ω)         | hit 102 mA / hold 51 mA / hit 100 ms |
| SOL_CH2    | VEV   | BinaryHitHold       | Parker C15 ≥0.040″ (R = 361 Ω) | hit  66 mA / hold 33 mA / hit 100 ms |
| SOL_CH3    | OLV_1 | BinaryHitHold       | Parker C21                     | same as OLV_2                         |
| **SOL_CH4** | **SEV**   | **VariableCurrent**     | **Parker VSO (R = 274 Ω)**     | **0..66 mA proportional via 7-bit CDR ladder** |
| SOL_CH5    | GDV   | BinaryHitHold       | Parker SRS 0.5 W (R = 1152 Ω)  | hit  21 mA / hold 10 mA / hit  30 ms  |
| SOL_CH6    | ORV   | BinaryHitHold       | Parker C15 ≥0.040″             | hit  66 mA / hold 33 mA / hit 100 ms |
| SOL_CH7    | AVV   | BinaryHitHold       | Parker C15 ≥0.040″             | same as ORV                           |

**Proportionals (TLE92466ED, channels 0–5):**

> The TLE92466ED PWM frequency is capped at 4 kHz by the chip itself (datasheet
> `PERIOD::kSpecMaxFrequency`). The catalog therefore drives every model at
> 4 kHz; the driver clamps and warns if you ever request more. LM-Pro keeps a
> tiny "telemetry" dither (1 mA @ 50 Hz) so the chip's `FB_I_AVG` / `FB_DC`
> averagers are populated — see `hf-tle92466ed-driver/docs/troubleshooting.md`.

| OUTn      | ID    | Model                       | Full-flow current | PWM     | Dither               |
|-----------|-------|-----------------------------|-------------------|---------|----------------------|
| PROP_CH0  | FRV   | Parker VSO MaxFlow 24 V     | 66 mA             | 4 kHz   | 4 mA @ 200 Hz        |
| PROP_CH1  | SPV_L | Parker VSO 24 V             | 66 mA             | 4 kHz   | 4 mA @ 200 Hz        |
| PROP_CH2  | FCV   | Parker VSO MaxFlow 24 V     | 66 mA             | 4 kHz   | 4 mA @ 200 Hz        |
| PROP_CH3  | LSV   | Parker VSO MaxFlow 24 V     | 66 mA             | 4 kHz   | 4 mA @ 200 Hz        |
| PROP_CH4  | SPV_H | Parker LM-Pro 24 V          | 115 mA            | 4 kHz   | 1 mA @ 50 Hz (telemetry) |
| PROP_CH5  | GFV   | Parker LM-Pro 24 V          | 115 mA            | 4 kHz   | 1 mA @ 50 Hz (telemetry) |

### 3.3 SEV — pseudo-proportional via MAX22200

SEV is a Parker VSO proportional valve, but because precise control is not
required at this site we route it through MAX22200 instead of taking up a
TLE92466ED channel. The driver mode is `VariableCurrent`:

- Every Apply() pushes `ConfigureSolenoidCdr(target_ma, target_ma, hit_time = 0)`.
- With `hit_time = 0` the MAX22200 skips the hit phase; the channel runs
  at a steady current equal to the requested setpoint.
- Resolution is 7-bit (≈ 128 steps over 0..IFS); for SEV's 0..66 mA range
  that's ~0.5 mA per step — more than enough for opening-percentage
  control.
- DPM and HHF are disabled for SEV (no hit phase to check; the dip
  signature DPM looks for is for binary on/off solenoids).

### 3.4 Per-valve atomic state (`Valve.h`)

Every valve carries:

```text
target_pos_         atomic<float>     — last setpoint requested (0..1)
target_dirty_       atomic<bool>      — pending application?
actual_pos_         atomic<float>     — last position the control thread set
state_              atomic<ValveState>— Idle / Active / Faulted / …
last_error_         atomic<hf_valve_err_t>
apply_count_        atomic<uint32_t>
apply_error_count_  atomic<uint32_t>
fault_count_        atomic<uint32_t>
last_fault_tick_ms_ atomic<uint64_t>
```

All `Get*` accessors use `memory_order_acquire`; the control / diagnostics
threads use `memory_order_release` on their writes. A single load yields a
self-consistent snapshot — control loops never need a mutex.

### 3.5 Application API

```cpp
auto& sys = manifold_valves::ManifoldValves::GetInstance();

// Binary solenoid:
sys.Get(manifold_valves::SolenoidId::OLV_1).RequestState(true);
bool actual_on = sys.Get(manifold_valves::SolenoidId::OLV_1).IsActuallyEnergised();

// Pseudo-proportional solenoid (SEV via MAX22200 CDR):
sys.Get(manifold_valves::SolenoidId::SEV).RequestPosition(0.42f);   // 0..1

// Precision proportional (TLE92466ED):
sys.Get(manifold_valves::ProportionalId::FCV).RequestPosition(0.42f);
float actual = sys.Get(manifold_valves::ProportionalId::FCV).GetActualPosition();

// Per-valve diagnostic snapshot:
auto sol_snap = sys.Get(manifold_valves::SolenoidId::OLV_1).Snapshot();
if (sol_snap.plunger_movement_fault) { /* handle stuck valve */ }

auto pro_snap = sys.Get(manifold_valves::ProportionalId::FCV).Snapshot();
uint16_t avg_current = pro_snap.average_current_raw;   // live measured

// Subsystem-wide health (run by ManifoldValvesDiagnosticsThread):
auto sub = valve_diag.GetSnapshot();
if (sub.tle_vbat_uv || sub.max_undervoltage) { /* supply problem */ }
```

All `Request*` and `Get*` calls are lock-free. The actual SPI traffic
happens on the next iteration of the control / diagnostics threads.

---

## 4. Diagnostics for medical-device safety

The diagnostics thread surfaces every flag the chip can publish — not just
"any fault" booleans. This is what allows the application layer to make
informed safety decisions:

### 4.1 Active diagnostics — react in software

| Signal | Meaning | Recommended response |
|--------|---------|----------------------|
| `Solenoid.plunger_movement_fault` | Plunger did not move under load | Mark valve mechanically stuck; alarm; safe-state the system. |
| `Solenoid.hit_not_reached` | HIT current never achieved within tHIT | Coil drift, supply sag or open coil. Re-attempt; alarm if persistent. |
| `Solenoid.open_load` | No load current path | Disconnected coil or wire. Disable channel, alarm. |
| `Solenoid.overcurrent` | Short to supply or shorted coil | Disable immediately; latch. |
| `Proportional.short_to_ground` | Short to ground | Latched fault. |
| `Proportional.over_temperature` | Channel die over-temp | Reduce setpoint; back-off duty; alarm. |
| `Proportional.current_regulation_warning` | Loop can't track current setpoint | Investigate wiring, supply, dither tuning. |
| `Subsystem.tle_vbat_uv` / `_ov` | Supply rail UV/OV | Stop actuating; raise system alarm. |
| `Subsystem.tle_spi_wd_error` | SPI watchdog tripped | Communication is failing; latch and re-init. |
| `Subsystem.tle_reg_ecc_err` / `tle_otp_ecc_err` | Register / OTP memory error | Treat as hard failure; latch. |
| `Subsystem.max_undervoltage` (UVM) | MAX22200 supply UV at boot or runtime | Re-read STATUS at init clears it; runtime UV is an alarm. |
| `Subsystem.max_communication_error` | SPI error detected by MAX22200 | Re-init; alarm if persistent. |

### 4.2 Preventive features (configured at startup)

- **Slew-rate control** enabled by default on every solenoid → cleaner
  switching edges, lower EMI, less acoustic clicking.
- **Open-load detection** enabled on every channel → catches a
  disconnected coil before it hides a real safety event.
- **Hit-current check (HHF)** enabled on every binary solenoid → if the
  hit current isn't reached within tHIT we know the solenoid never
  energised.
- **DPM (plunger movement)** enabled on all 5 binary cartridge solenoids.
  Tuned via `ConfigureGlobalDpm(30 mA, 8 mA, 0.5 ms)` at boot — adjust
  per the actual mechanical signature.
- **OLSG warning** on every TLE92466 channel → early indication of
  open-load / short-to-ground precursors.
- **CRC + SPI watchdog + clock watchdog** on TLE92466ED globally
  (configured by `Flux::ValveManager` on init).

---

## 5. Failure / degraded-mode behaviour

If `Flux::ValveManager` fails to bring up either driver IC (the WH bench
in its current state shows exactly this — the hardware isn't connected):

- `ValveManager::IsInitialized()` returns `false`.
- `ManifoldValvesControlThread` keeps stepping but skips `Apply()` calls.
- `ManifoldValvesDiagnosticsThread` reports `manager_ready = false` and zeroes the
  per-driver readiness flags / fault snapshots.
- All `Valve::Get*` accessors keep returning the last-published soft
  state (`Uninitialized` / 0 / `VALVE_SUCCESS`).

This means an outer control loop can be developed and unit-tested against
the same atomic API even when the valve hardware isn't wired up.

---

## 6. Boot self-test (`ManifoldValvesSelfTest`)

Every boot, after the pressure thread is calibrated and the valve control +
diagnostics threads are running, the application calls
`ManifoldValvesSelfTest::Run()`. This single function:

1. **Walks every solenoid in the catalog** (`SolenoidId::OLV_2 → AVV`) and:
   - Prints a banner ("Connect ONLY SOL '<label>' …") with a configurable
     prompt countdown so the operator can move the manifold tube.
   - Cycles the channel `solenoid_cycles` × (ON `solenoid_on_ms` / OFF
     `solenoid_off_ms`) — exact same test pattern as the
     `c21_cycle_test.cpp` example in the MAX22200 driver.
   - After each ON/OFF transition waits two full diagnostics-thread sweeps
     and logs a per-channel snapshot (`tgt`, `act`, state, OCP / HHF / OLF /
     DPM flags, sticky `fc`).

2. **Walks every proportional in the catalog** (`ProportionalId::FRV → GFV`)
   and:
   - Prompts the operator (same pattern).
   - Ramps `0 → full-flow → 0` in `proportional_steps` increments with a
     `proportional_settle_ms` settle between each setpoint — same pattern as
     the `lm_pro_cycle_test.cpp` example in the TLE92466ED driver.
   - Logs the chip's live telemetry at every step: commanded current,
     `FB_I_AVG` raw, `FB_DC` raw, `last_err`, every fault flag and every
     warning.

3. **Aggregates a per-valve PASS/FAIL** based on:
   - `apply_error_count_` delta (ANY non-success Apply during the cycle).
   - `fault_count_` delta (any per-channel fault flag set during the cycle).
   - The most recent diagnostic snapshot (`Snapshot::any_fault()`).
   The summary block at the end prints OK / FAIL plus a one-line fault
   description for each valve.

### 6.1 Operator workflow on a manifold-limited rig

The bench can only mechanically attach **one valve to the manifold at a
time**. The defaults (`prompt_wait_ms = 3000`, `inter_valve_pause_ms = 4000`)
are tuned for that workflow:

| Phase                           | Duration |
|---------------------------------|----------|
| Per-valve operator prompt       | 3 s      |
| Per-valve test (solenoid)       | ~12 s (3 × on/off cycles) |
| Per-valve test (proportional)   | ~5 s   (22 ramp setpoints × 0.15 s) |
| Pause between valves            | 4 s      |
| **Full sweep** (7 sol + 6 prop) | **≈ 3 min** |

Tweak the constants in `MakeSelfTestPlan()` at the top of
`manifold_valves_pressure_control.cpp` to:
- skip valves that are not yet stocked (set the `enable_*` flag to `false`);
- shorten / lengthen the pulse profiles or ramp resolution;
- disable the operator prompt (`prompt_wait_ms = 0`) for fully unattended
  runs.

To skip the self-test entirely set
`kRunSelfTestAtBoot = false` at the top of the same file.

### 6.2 What the self-test catches

The self-test is the cheapest way to **catch a stuck plunger, an unconnected
coil, a damaged driver IC, or an audibly dead solenoid** before the rig
starts running pressure-control loops. Specifically:

| Symptom                                        | Catalog check that surfaces it          |
|------------------------------------------------|------------------------------------------|
| Coil disconnected / connector reversed         | `open_load` / `open_load_short_ground` flag, `last_err = "Open load detected"` |
| Coil shorted to GND                            | `short_to_ground` flag (TLE92466ED)      |
| Plunger stuck in valve seat                    | `plunger_movement_fault` (MAX22200 DPM)  |
| Solenoid hit current never reached             | `hit_not_reached` (MAX22200 HHF)         |
| TLE chip not in correct mode                   | `last_err = "Operation not allowed in mode"` (Apply-time error count) |
| Supply browning out                            | `SubsystemFaultSnapshot.max_undervoltage` / `tle_vbat_uv` |
| TLE PWM regulation can't track                 | `pwm_regulation_warning` |
| TLE current loop can't track                   | `current_regulation_warning` |
| Over-temperature, ECC error                    | `over_temperature` / `ot_warning` / `tle_reg_ecc_err` |

> ⚠️  **Configuration-Mode round-trip:** Each `ProportionalValve` enters
> Configuration Mode, pushes its `ChannelConfig` + PWM + dither, then
> re-enters Mission Mode. This is required because the TLE92466ED rejects
> any `CH_x_CTRL` write while in Mission Mode and the handler enters
> Mission Mode at init time. Skipping this round-trip is what produced the
> infamous "every Apply returns Operation-not-allowed-in-mode" symptom.

---

## 7. Extension recipes

### 6.1 Add a binary solenoid

1. Add an enumerator in `manifold_valves::SolenoidId` before `Count`.
2. Append a row in `kSolenoidMap` referencing one of the `kSpec*` model
   structs and an unused `ValveChannel::SOL_CHn`.

### 6.2 Add a pseudo-proportional via MAX22200

Same as above, but:
- pick `SolenoidDriveMode::VariableCurrent`,
- use a model spec with `full_flow_current_ma` set and
  `hit_current_ma = hold_current_ma = 0`,
- disable `plunger_movement_detection` and `hit_current_check` in the
  channel features (the current waveform in continuous-current mode is
  not what those algorithms were designed for).

### 6.3 Add a precision proportional

1. Add an enumerator in `manifold_valves::ProportionalId` before `Count` (TLE92466
   has 6 channels — if you exceed that you need a second IC).
2. Append a row in `kProportionalMap` with the right model spec and
   feature gates.

### 6.4 Use a different model

Edit (or add) a `SolenoidModelSpec` / `ProportionalModelSpec` literal at
the top of `ManifoldValvesCatalog.h`, then point the relevant table row at it.
Only the catalog file changes.

### 6.5 Re-tune diagnostics

- DPM parameters: `ManifoldValves::ConfigureGlobalDpm(start, dip, debounce)`.
  Default 30 mA / 8 mA / 0.5 ms tuned for the C21 hold band; adjust per
  the actual mechanical signature.
- Per-channel feature gates: edit the `Max22200ChannelFeatures` /
  `Tle92466ChannelFeatures` row in the bench map.
- Diagnostics thread cadence: `ManifoldValvesDiagnosticsThread::SetPeriodMs(n)` —
  default 100 ms, drop to 20 ms for closer monitoring or raise to 500 ms
  to lighten SPI load.

---

## 8. Files in this folder

| File | Purpose |
|------|---------|
| `manifold_valves_pressure_control.cpp` | App entry: brings up Flux, calibrates pressure sensors, configures DPM, starts control + diagnostics threads. |
| `PressureSensorConfig.h` | Sensor enums + ratiometric range + calibration config types. |
| `PressureSensor.{h,cpp}` | Per-channel pressure sensor: IIR filter, atomic publish. |
| `PressureSensorThread.{h,cpp}` | 1 kHz BaseThread + Auto-1 ADS7952 batch read. |
| `ManifoldValvesCatalog.h` | Compile-time enums + Parker valve datasheet specs + bench map + per-channel diagnostic feature gates. |
| `Valve.h` | Base valve class (atomic state, dirty flag, snapshot types). |
| `SolenoidValve.h` | MAX22200-driven valve — handles BinaryHitHold and VariableCurrent modes; decodes per-channel OCP/HHF/OLF/DPM. |
| `ProportionalValve.h` | TLE92466ED-driven precision proportional; full per-channel ChannelDiagnostics surfacing. |
| `ManifoldValves.h` | Singleton container, O(1) lookup, iteration helpers, `ConfigureGlobalDpm`. |
| `ManifoldValvesControlThread.h` | 100 Hz BaseThread that pushes dirty setpoints. |
| `ManifoldValvesDiagnosticsThread.h` | 10 Hz BaseThread; per-valve + per-driver-IC fault decoding into `SubsystemFaultSnapshot`. |
| `ManifoldValvesSelfTest.h` | Boot-time per-valve cycler — solenoid ON/OFF and proportional 0→full→0 ramps with full diagnostic capture. Header-only; configured from `MakeSelfTestPlan()` in the app entry. |
| `README.md` | This document. |
