# WH Valve & Pressure Sensor Application

This example is the WH bench-test acquisition + actuation stack. It owns:

- **8 Honeywell SSC TruStability** ratiometric pressure transducers on the
  ADS7952 12-bit SPI ADC.
- **5 Parker C15/C21** cartridge solenoid valves driven through the **MAX22200**
  octal solenoid driver.
- **7 Parker VSO / VSO LowPro / LM-Pro** proportional valves driven through
  the **TLE92466ED** 6-channel current-controlled driver.

Everything hangs off the existing Flux V1 HAL — this folder contributes the
**application-level abstractions** (named valves, hit/hold profiles,
positional API, dedicated control + diagnostics threads).

---

## 1. Architecture overview

```
┌───────────────────────────────────────────────────────────────────┐
│  Application / control-loop threads (any rate)                    │
│   - PressureSensor::GetPressureFiltered()      lock-free          │
│   - WhValveSystem::Get(…).RequestPosition()    lock-free          │
│   - WhValveSystem::Get(…).GetActualPosition()  lock-free          │
└───────────────────────────────────────────────────────────────────┘
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
┌───────────────────────────────────────────────────────────────────┐
│ WhDiagnosticsThread (10 Hz)                                       │
│  - Per-valve OnDiagnostics() → fault counters                     │
│  - Aggregates totals into DiagnosticsSnapshot                     │
│  - (Hook for ADS7952 alarms — see TODO in WhDiagnosticsThread.h)  │
└───────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌───────────────────────────────────────────────────────────────────┐
│ Flux::ValveManager  +  Flux::AdcManager  +  Flux::CommChannels    │
│  - MAX22200 (solenoids), TLE92466ED (proportionals), ADS7952 (ADC)│
└───────────────────────────────────────────────────────────────────┘
```

### 1.1 Why three threads?

| Thread | Period | Job |
|--------|--------|-----|
| Pressure sampling | 1 ms | Match the SSC ASIC's internal 1 kHz pressure update; produce filtered, atomic snapshots that any control loop can consume lock-free. |
| Valve control | 10 ms | Push every dirty valve setpoint to MAX22200 / TLE92466ED. Doing this at 100 Hz keeps SPI traffic low while still feeling instant to a 500 Hz outer control loop. |
| Diagnostics | 100 ms | Read driver-IC fault registers and (planned) ADS7952 alarm bits without ever stalling the higher-rate loops on multi-register SPI reads. |

This separation means the control loop **never** has to read fault registers
itself — that's the diagnostics thread's responsibility — and the
diagnostics thread **never** delays the application's set/get path.

---

## 2. Pressure sensors

Honeywell SSC TruStability sensors output **10 % – 90 % of V_supply** over
the configured pressure range (transfer function "A"), with output clipping
limits at 2.5 % and 97.5 % of V_supply. Resolution ≥ 0.03 % FSS, accuracy
±0.25 % FSS BFSL, total error band ±2 % FSS, internal ASIC update ≈ 1 kHz,
response time ≈ 1 ms.

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

### 2.1 Acquisition pipeline

`PressureSensorThread::Step()` runs every 1 ms and:

1. Calls `Ads7952Handler::ReadMultipleChannels` (Auto-1 mode) — **all 8
   channels in a single SPI transaction**.
2. Feeds each raw voltage to `PressureSensor::Update(voltage_raw)`.
3. `Update()` runs a single-pole IIR low-pass:
   `y[n] = α·x[n] + (1−α)·y[n−1]` with `α = 1 − exp(−2π·fc/fs)`. Default
   `fc = 100 Hz`, `fs = 1 kHz` ⇒ `α ≈ 0.467`.
4. Filtered voltage → calibrated v→p anchor → atomic publish.

### 2.2 Calibration

| Phase | What it does |
|-------|--------------|
| **VA** | Average atmospheric voltage across all channels marked `use_for_va_cal` and infer the actual supply voltage. Required for ratiometric scaling. |
| **Zero** | For each channel, capture the atmospheric voltage and store it as `zero_offset_voltage_`. The sensor doesn't have to physically output `5 % · VA` at 0 PSI; we anchor the conversion at the observed atmospheric voltage with slope `(P_max − P_min) / (V_max − V_min)`. |
| **Swap** | Optional. Tracks min/max readings during a deliberate pressurisation cycle to detect swapped tubing on single-ended sensors. |

### 2.3 Lock-free read API for control loops

```cpp
PressureSensor& s = *g_sensors[i];
float pressure_psi = s.GetPressureFiltered();   // atomic load, acquire ordering
float voltage_v    = s.GetVoltageFiltered();
```

A 500 Hz control loop just calls these — no mutex, no SPI traffic.

---

## 3. Valves

### 3.1 Compile-time catalog (`WhValveCatalog.h`)

Single source of truth for:

- **Named IDs** — `whvalve::SolenoidId` (5 valves) and
  `whvalve::ProportionalId` (7 valves).
- **Datasheet specs** — `kSpecC21_24V`, `kSpecC15_24V_Small`,
  `kSpecC15_24V_Large`, `kSpecVSO_24V`, `kSpecVSOLowPro_24V`,
  `kSpecLMPro_24V`. Each carries the rated voltage, coil resistance,
  computed full-flow / hit / hold currents, recommended PWM frequency
  and (for VSO) dither parameters straight out of the catalogs.
- **Bench mapping** — `kSolenoidMap` and `kProportionalMap` glue each named
  valve to a Flux `ValveChannel` and one of the model specs. Add or remove
  a valve by editing exactly two lines (the enum and the corresponding
  table row); nothing else changes.

### 3.2 Hit-and-Hold for solenoids (C15 / C21)

Each Parker datasheet recommends:

- **Hit** at 100 % of rated voltage for **≥ 100 ms**.
- **Hold** at 50 % of rated voltage (i.e. ~50 % of hit current).
- **PWM ≥ 1 kHz** during hold.

The catalog converts that into `hit_current_ma` and `hold_current_ma` for
the chosen 24 V coil:

| Model              | Coil R | Hit current | Hold current |
|--------------------|--------|-------------|--------------|
| C21 (24 V)         | 235 Ω  | 102 mA      | 51 mA        |
| C15 0.020″ (24 V)  | 525 Ω  | 46 mA       | 23 mA        |
| C15 ≥0.040″ (24 V) | 361 Ω  | 66 mA       | 33 mA        |

`SolenoidValve::Apply()` configures the MAX22200 channel in CDR mode with
those numbers the very first time it's called, then just toggles
`EnableChannel` / `DisableChannel` on subsequent setpoint changes.

### 3.3 Current control for proportionals (VSO / LM-Pro)

The proportional position **0..1** linearly maps to current
**0 mA … full_flow_current_ma**. At 24 V:

| Model              | Coil R | Full-flow current | PWM      |
|--------------------|--------|-------------------|----------|
| VSO (24 V)         | 274 Ω  | 66 mA             | 8 kHz    |
| VSO LowPro (24 V)  | 179 Ω  | 75 mA             | 10 kHz   |
| LM-Pro (24 V)      | 148 Ω  | 115 mA            | ≥ 5 kHz  |

`ProportionalValve::Apply()`:

1. (First pass only) configures TLE92466ED PWM frequency + optional dither
   (VSO family only — LM-Pro is a linear motor and doesn't benefit).
2. Translates the requested 0..1 position to mA via
   `PositionToCurrentMa()`.
3. `SetProportionalCurrent(channel, mA)` (or `DisableChannel` if `pos = 0`).

> **Hardware limit.** TLE92466ED has 6 channels and the bench currently
> requests 7 proportionals. `SPV_L` is therefore declared `Unmapped` in
> `kProportionalMap`; calls to it return
> `VALVE_ERR_DRIVER_NOT_AVAILABLE` and the diagnostics thread reports it
> as `Uninitialized`. Replace `ValveChannel::CHANNEL_COUNT` in
> `kProportionalMap` once a second TLE92466ED IC (or another suitable
> driver) is added.

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
threads use `memory_order_release` on their writes. Within the same valve,
a single load yields a self-consistent snapshot — control loops never need
a mutex.

### 3.5 Application API

```cpp
auto& sys = whvalve::WhValveSystem::GetInstance();

// Solenoid: boolean state.
sys.Get(whvalve::SolenoidId::OLV_1).RequestState(true);
bool actual_on = sys.Get(whvalve::SolenoidId::OLV_1).IsActuallyEnergised();

// Proportional: 0..1 position.
sys.Get(whvalve::ProportionalId::FCV).RequestPosition(0.42f);
float actual = sys.Get(whvalve::ProportionalId::FCV).GetActualPosition();

// Status / diagnostics:
auto state = sys.Get(whvalve::ProportionalId::FCV).GetState();
auto err   = sys.Get(whvalve::ProportionalId::FCV).GetLastError();
uint32_t f = sys.Get(whvalve::ProportionalId::FCV).GetFaultCount();
```

Both `RequestState` / `RequestPosition` and the `Get*` calls are lock-free.
The actual SPI write happens on the next iteration of
`WhValveControlThread`.

---

## 4. Threads

### 4.1 `PressureSensorThread`

- Period: **1 ms** (`kPressureSampleIntervalMs`).
- IIR cutoff: **100 Hz** (`kPressureFilterCutoffHz`).
- Status print: every 100 cycles (~5 Hz).
- Stack: 8 KiB.

### 4.2 `WhValveControlThread`

- Period: **10 ms** (configurable via constructor).
- Each iteration: `system.ForEachValve([] (Valve& v) { if v.ConsumeDirty()) v.Apply(mgr); });`.
- Reports `mgr_was_ready_` so the application can detect whether the
  underlying ICs ever came up.
- Stack: 6 KiB, priority 7.

### 4.3 `WhDiagnosticsThread`

- Period: **100 ms** (configurable).
- Walks every valve, calls `OnDiagnostics(mgr)` to refresh per-channel
  fault counters; aggregates totals into a `DiagnosticsSnapshot` exposed
  via lock-free `GetSnapshot()`.
- Hooks into `Ads7952Handler` for future alarm scraping (see TODO in
  `WhDiagnosticsThread.h`).
- Stack: 6 KiB, priority 5 (lower than the control thread).

---

## 5. Failure / degraded-mode behaviour

The Flux `ValveManager` may fail to come up if either MAX22200 or
TLE92466ED is not present (the WH bench in its current state shows exactly
this — the hardware isn't connected). The application stays alive:

- `ValveManager::IsInitialized()` returns `false`.
- `WhValveControlThread` keeps stepping but skips `Apply()` calls.
- `WhDiagnosticsThread` reports `manager_ready = false` in the snapshot.
- All `Valve::Get*` accessors keep returning the last published state
  (`Uninitialized` / 0 / `VALVE_SUCCESS`).

This means an outer control loop can be developed and unit-tested against
the same atomic API even when the valve hardware isn't wired up.

---

## 6. Extension recipes

### 6.1 Add a solenoid

1. Add an enumerator in `whvalve::SolenoidId`, just before `Count`.
2. Append a row in `kSolenoidMap` referencing one of the `kSpec*` model
   structs and an unused `ValveChannel::SOL_CHn`.
3. Done — the system, threads and lookups all scale automatically.

### 6.2 Add a proportional

Same procedure with `whvalve::ProportionalId` and `kProportionalMap`. If
all 6 TLE92466ED channels are already taken, leave the mapping at
`ValveChannel::CHANNEL_COUNT` (the system will mark it `Uninitialized`).

### 6.3 Use a different model

Edit (or add) a `SolenoidModelSpec` / `ProportionalModelSpec` literal at
the top of `WhValveCatalog.h`, then point the relevant table row at it.
Only the catalog file changes.

### 6.4 Re-tune calibration / sample rate

- Pressure: change `kPressureSampleIntervalMs` (1 ms = 1 kHz) or
  `kPressureFilterCutoffHz` in `wh_valve_pressure_control.cpp`. The
  worker re-derives `α` automatically.
- Valves: change the ms argument when constructing `WhValveControlThread`
  or call `valve_ctrl.SetSamplePeriodMs(…)` at runtime.

---

## 7. Files in this folder

| File | Purpose |
|------|---------|
| `wh_valve_pressure_control.cpp` | App entry; brings up Flux, calibrates pressure sensors, starts threads. |
| `PressureSensorConfig.h` | Sensor enums + ratiometric range + calibration config types. |
| `PressureSensor.{h,cpp}` | Per-channel pressure sensor: IIR filter, atomic publish. |
| `PressureSensorThread.{h,cpp}` | 1 kHz BaseThread + Auto-1 ADS7952 batch read + dispatch. |
| `WhValveCatalog.h` | Compile-time enums + Parker valve datasheet specs + bench map. |
| `Valve.h` | Base valve class (atomic state, dirty flag, ApplyDispatch hooks). |
| `SolenoidValve.h` | C15/C21 hit/hold via MAX22200. |
| `ProportionalValve.h` | VSO / LM-Pro current control via TLE92466ED. |
| `WhValveSystem.h` | Singleton container, O(1) lookup, iteration helpers. |
| `WhValveControlThread.h` | 100 Hz BaseThread that pushes dirty setpoints. |
| `WhDiagnosticsThread.h` | 10 Hz BaseThread that refreshes per-valve fault counters. |
| `README.md` | This file. |
