# WH Valve & Pressure Sensor Application

This example is the WH bench-test acquisition + actuation stack. It owns:

- **8 Honeywell SSC TruStability** ratiometric pressure transducers on the
  ADS7952 12-bit SPI ADC.
- **6 Parker C15/C21 + VSO** valves driven through the **MAX22200** octal
  solenoid driver (5 binary hit/hold + 1 pseudo-proportional).
- **6 Parker VSO / VSO LowPro / LM-Pro** precision proportional valves
  driven through the **TLE92466ED** 6-channel current-controlled driver.

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
│   - WhValveSystem::Get(SolenoidId).RequestState/RequestPosition    │
│   - WhValveSystem::Get(ProportionalId).RequestPosition             │
│   - .GetActualPosition() / .GetState() / .Snapshot()  lock-free    │
└────────────────────────────────────────────────────────────────────┘
          │                                         │
          ▼                                         ▼
┌──────────────────────────────┐   ┌──────────────────────────────┐
│ PressureSensorThread (1 kHz) │   │ WhValveControlThread (100 Hz)│
│  - Auto-1 batched ADS7952    │   │  - ConsumeDirty() per valve  │
│  - Per-channel single-pole   │   │  - Apply() → ValveManager    │
│    IIR + atomic publish      │   │  - Updates actual_position   │
└──────────────────────────────┘   └──────────────────────────────┘
                              │
                              ▼
┌────────────────────────────────────────────────────────────────────┐
│ WhDiagnosticsThread (10 Hz)                                        │
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
| **DPM (Detection of Plunger Movement)** — global algorithm (CFG_DPM) | `WhValveSystem::ConfigureGlobalDpm(start_ma, dip_ma, debounce_ms)` configures ISTART / IPTH / TDEB. Per-channel DPM_EN bits set from the catalog's feature gate. |
| **Per-channel faults** (FAULT register) | `SolenoidValve::Snapshot()` returns `overcurrent`, `hit_not_reached`, `open_load`, `plunger_movement_fault` plus sticky counters. |
| **Global health** (STATUS register) | `WhDiagnosticsThread::SubsystemFaultSnapshot.max_*` carries OVT, COMER, UVM, the global OCP/OLF/HHF/DPM bits and the active-channels mask. |

#### TLE92466ED (six-channel current-controlled, 6 channels — using all 6)

| Capability | Use here |
|------------|----------|
| **Closed-loop current control** | `ValveManager::SetProportionalCurrent` per valve, called every dirty Apply. |
| **PWM frequency** (mantissa+exponent encoded) | Each valve's `pwm_hz` from the model spec is pushed once via `ValveManager::ConfigureProportionalPwm`. |
| **Dither** (step size + steps + flat period) | Pushed once via `ValveManager::ConfigureProportionalDither` for VSO-class valves. LM-Pro skips dither (it's a linear motor). |
| **Slew rate / diagnostic current / open-load threshold / OLSG warning** | Configured via `tle92466ed::ChannelConfig` from `Tle92466ChannelFeatures`. |
| **Per-channel diagnostics** | `ProportionalValve::Snapshot()` carries every flag from `tle92466ed::ChannelDiagnostics`: `overcurrent`, `short_to_ground`, `open_load`, `over_temperature`, `open_load_short_ground`, `ot_warning`, `current_regulation_warning`, `pwm_regulation_warning`, `olsg_warning`, plus live measurements `average_current_raw`, `duty_cycle_raw`, `vbat_feedback_raw`. |
| **Global health** (FaultReport) | `WhDiagnosticsThread::SubsystemFaultSnapshot.tle_*` carries VBAT/VIO/VDD UV/OV, central OT warning/error, clock fault, SPI watchdog, POR/reset events, register/OTP ECC errors, supply-NOK summaries. |

### 3.2 Bench mapping

**Solenoids (MAX22200, channels 0–5):**

| ID    | Channel  | Drive mode      | Model                         | Datasheet currents (24 V) |
|-------|----------|-----------------|-------------------------------|--------------------------|
| OLV_1 | SOL_CH0  | BinaryHitHold   | Parker C21 (R = 235 Ω)        | hit 102 mA / hold 51 mA / hit 100 ms |
| OLV_2 | SOL_CH1  | BinaryHitHold   | Parker C21                    | same |
| VEV   | SOL_CH2  | BinaryHitHold   | Parker C15 ≥0.040″ (R=361 Ω)  | hit 66 mA / hold 33 mA / hit 100 ms |
| ORV   | SOL_CH3  | BinaryHitHold   | Parker C15 ≥0.040″            | same |
| GDV   | SOL_CH4  | BinaryHitHold   | Parker C15 0.020″ (R=525 Ω)   | hit 46 mA / hold 23 mA / hit 100 ms |
| **SEV** | **SOL_CH5** | **VariableCurrent** | **Parker VSO (R=274 Ω)** | **0..66 mA proportional via 7-bit CDR ladder** |

**Proportionals (TLE92466ED, channels 0–5):**

| ID    | Channel    | Model                | Full-flow current | PWM | Dither |
|-------|------------|----------------------|-------------------|-----|--------|
| FCV   | PROP_CH0   | Parker LM-Pro 24 V   | 115 mA            | 5 kHz   | – |
| FRV   | PROP_CH1   | Parker LM-Pro 24 V   | 115 mA            | 5 kHz   | – |
| LSV   | PROP_CH2   | Parker VSO 24 V      | 66 mA             | 8 kHz   | 4 mA @ 200 Hz |
| GFV   | PROP_CH3   | Parker VSO 24 V      | 66 mA             | 8 kHz   | 4 mA @ 200 Hz |
| SPV_H | PROP_CH4   | Parker VSO LowPro    | 75 mA             | 10 kHz  | 4 mA @ 200 Hz |
| SPV_L | PROP_CH5   | Parker VSO LowPro    | 75 mA             | 10 kHz  | 4 mA @ 200 Hz |

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
auto& sys = whvalve::WhValveSystem::GetInstance();

// Binary solenoid:
sys.Get(whvalve::SolenoidId::OLV_1).RequestState(true);
bool actual_on = sys.Get(whvalve::SolenoidId::OLV_1).IsActuallyEnergised();

// Pseudo-proportional solenoid (SEV via MAX22200 CDR):
sys.Get(whvalve::SolenoidId::SEV).RequestPosition(0.42f);   // 0..1

// Precision proportional (TLE92466ED):
sys.Get(whvalve::ProportionalId::FCV).RequestPosition(0.42f);
float actual = sys.Get(whvalve::ProportionalId::FCV).GetActualPosition();

// Per-valve diagnostic snapshot:
auto sol_snap = sys.Get(whvalve::SolenoidId::OLV_1).Snapshot();
if (sol_snap.plunger_movement_fault) { /* handle stuck valve */ }

auto pro_snap = sys.Get(whvalve::ProportionalId::FCV).Snapshot();
uint16_t avg_current = pro_snap.average_current_raw;   // live measured

// Subsystem-wide health (run by WhDiagnosticsThread):
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
- `WhValveControlThread` keeps stepping but skips `Apply()` calls.
- `WhDiagnosticsThread` reports `manager_ready = false` and zeroes the
  per-driver readiness flags / fault snapshots.
- All `Valve::Get*` accessors keep returning the last-published soft
  state (`Uninitialized` / 0 / `VALVE_SUCCESS`).

This means an outer control loop can be developed and unit-tested against
the same atomic API even when the valve hardware isn't wired up.

---

## 6. Extension recipes

### 6.1 Add a binary solenoid

1. Add an enumerator in `whvalve::SolenoidId` before `Count`.
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

1. Add an enumerator in `whvalve::ProportionalId` before `Count` (TLE92466
   has 6 channels — if you exceed that you need a second IC).
2. Append a row in `kProportionalMap` with the right model spec and
   feature gates.

### 6.4 Use a different model

Edit (or add) a `SolenoidModelSpec` / `ProportionalModelSpec` literal at
the top of `WhValveCatalog.h`, then point the relevant table row at it.
Only the catalog file changes.

### 6.5 Re-tune diagnostics

- DPM parameters: `WhValveSystem::ConfigureGlobalDpm(start, dip, debounce)`.
  Default 30 mA / 8 mA / 0.5 ms tuned for the C21 hold band; adjust per
  the actual mechanical signature.
- Per-channel feature gates: edit the `Max22200ChannelFeatures` /
  `Tle92466ChannelFeatures` row in the bench map.
- Diagnostics thread cadence: `WhDiagnosticsThread::SetPeriodMs(n)` —
  default 100 ms, drop to 20 ms for closer monitoring or raise to 500 ms
  to lighten SPI load.

---

## 7. Files in this folder

| File | Purpose |
|------|---------|
| `wh_valve_pressure_control.cpp` | App entry: brings up Flux, calibrates pressure sensors, configures DPM, starts control + diagnostics threads. |
| `PressureSensorConfig.h` | Sensor enums + ratiometric range + calibration config types. |
| `PressureSensor.{h,cpp}` | Per-channel pressure sensor: IIR filter, atomic publish. |
| `PressureSensorThread.{h,cpp}` | 1 kHz BaseThread + Auto-1 ADS7952 batch read. |
| `WhValveCatalog.h` | Compile-time enums + Parker valve datasheet specs + bench map + per-channel diagnostic feature gates. |
| `Valve.h` | Base valve class (atomic state, dirty flag, snapshot types). |
| `SolenoidValve.h` | MAX22200-driven valve — handles BinaryHitHold and VariableCurrent modes; decodes per-channel OCP/HHF/OLF/DPM. |
| `ProportionalValve.h` | TLE92466ED-driven precision proportional; full per-channel ChannelDiagnostics surfacing. |
| `WhValveSystem.h` | Singleton container, O(1) lookup, iteration helpers, `ConfigureGlobalDpm`. |
| `WhValveControlThread.h` | 100 Hz BaseThread that pushes dirty setpoints. |
| `WhDiagnosticsThread.h` | 10 Hz BaseThread; per-valve + per-driver-IC fault decoding into `SubsystemFaultSnapshot`. |
| `README.md` | This document. |
