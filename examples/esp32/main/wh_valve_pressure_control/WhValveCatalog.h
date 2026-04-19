/**
 * @file WhValveCatalog.h
 * @brief Compile-time catalog of all WH bench valves and their datasheet specs.
 *
 * @details Single source of truth for the bench valve set:
 *
 *   - Two named ID enums (one per valve family) that drive O(1) lookup.
 *
 *   - A model database for the Parker valves we drive (C21/C15 cartridge
 *     solenoids, VSO/VSO LowPro/LM-Pro proportionals) with the electrical
 *     numbers pulled directly from each datasheet. Adding/removing a model
 *     is a single literal change here; nothing else in the system changes.
 *
 *   - Two compile-time mapping tables (`kSolenoidMap`, `kProportionalMap`)
 *     assigning each named valve to a Flux::ValveChannel and a model.
 *
 *   - Per-valve diagnostic feature gates (open-load, plunger-movement, hit-
 *     current check, slew-rate control) — opt in only what the underlying
 *     load actually supports so we don't get spurious faults.
 *
 * @par Driver IC mapping (Flux V1 board)
 *
 *   - **MAX22200** (8 channels) drives every solenoid:
 *       * `BinaryHitHold` mode — classic on/off cartridge solenoids
 *         (C21 / C15) using a calibrated hit pulse + reduced hold current
 *         (datasheet "Optional Reduced Power Control Method").
 *       * `VariableCurrent` mode — for proportional valves that are
 *         being controlled with the MAX22200's 7-bit CDR ladder for
 *         coarse 0–127 step current control. The driver writes the same
 *         setpoint to both `hit` and `hold` fields with `hit_time = 0`,
 *         producing a sustained constant current proportional to the
 *         requested position.
 *
 *   - **TLE92466ED** (6 channels) drives the precision proportionals
 *     (FCV/FRV/LSV/GFV/SPV_H/SPV_L) with full current control + PWM +
 *     dither + per-channel current/duty/temperature feedback.
 *
 * @par Acquisition references
 *
 *   - Honeywell SSC TruStability — sensor side (see `PressureSensorConfig.h`).
 *   - Parker C21 cartridge solenoid (Hit-and-Hold §"Optional Reduced Power
 *     Control Method", 2.5 W full hit, 50 % hold, ≥ 1 kHz PWM).
 *   - Parker C15 cartridge solenoid (Hit-and-Hold §"Optional Reduced Power
 *     Control Method", 1.1–1.7 W full hit, 50 % hold, ≥ 1 kHz PWM).
 *   - Parker VSO TruStability proportional (Table 2, current-controlled,
 *     5–12 kHz PWM).
 *   - Parker LM-Pro linear-motor proportional (Table 1, current-controlled,
 *     5 kHz PWM minimum).
 *
 * @copyright HardFOC (internal use only)
 * @date 2026
 */

#pragma once

#include "managers/ValveManager.h"

#include <array>
#include <cstdint>
#include <string_view>

namespace whvalve {

//==============================================================================
// PER-VALVE NAMED IDs (compile-time fixed)
//==============================================================================

/**
 * @brief WH bench valves driven by the MAX22200 (octal solenoid driver).
 *
 *   - OLV_1 / OLV_2 / VEV / ORV / GDV — pure on/off cartridge solenoids
 *     (Parker C21 / C15) controlled with a calibrated hit-and-hold profile.
 *   - SEV — proportional valve being driven via the MAX22200's 7-bit CDR
 *     ladder for low-precision opening-percentage control. SEV does not
 *     need the precision of the TLE92466ED on this bench, so we save a
 *     TLE channel by routing it through MAX22200.
 *
 * @note Adding a new MAX22200-driven valve:
 *       1. Add an enumerator above `Count` here.
 *       2. Add a row in `kSolenoidMap` (below) referencing a model spec.
 */
enum class SolenoidId : uint8_t {
    OLV_1 = 0,   ///< Outlet valve #1 (Parker C21, BinaryHitHold)
    OLV_2,       ///< Outlet valve #2 (Parker C21, BinaryHitHold)
    VEV,         ///< Vent valve (Parker C15 large orifice, BinaryHitHold)
    ORV,         ///< Overflow / outflow valve (Parker C15 large orifice, BinaryHitHold)
    GDV,         ///< Gas/drain/discharge valve (Parker C15 small orifice, BinaryHitHold)
    SEV,         ///< Selector valve — coarse proportional via MAX22200 CDR (VariableCurrent)
    Count        ///< Sentinel — keep last.
};

/**
 * @brief WH bench precision proportional valves driven by the TLE92466ED.
 *
 * Each channel has full closed-loop current control with PWM frequency,
 * optional dither, slew-rate control and rich per-channel diagnostics.
 *
 * @note With SEV moved to the MAX22200, the bench has exactly 6 precision
 *       proportionals which fits the TLE92466ED's 6 channels with no
 *       unmapped slots.
 */
enum class ProportionalId : uint8_t {
    FCV   = 0,   ///< Flow control valve (Parker LM-Pro 24 V)
    FRV,         ///< Flow regulator valve (Parker LM-Pro 24 V)
    LSV,         ///< Load-sensing valve (Parker VSO 24 V)
    GFV,         ///< Gas flow valve (Parker VSO 24 V)
    SPV_H,       ///< Supply pressure valve — high range (Parker VSO LowPro 24 V)
    SPV_L,       ///< Supply pressure valve — low  range (Parker VSO LowPro 24 V)
    Count        ///< Sentinel — keep last.
};

/// Compile-time count helpers.
inline constexpr std::size_t kNumSolenoids     = static_cast<std::size_t>(SolenoidId::Count);
inline constexpr std::size_t kNumProportionals = static_cast<std::size_t>(ProportionalId::Count);

//==============================================================================
// MAX22200 DRIVE-MODE TAXONOMY
//==============================================================================

/**
 * @brief How a MAX22200 channel is driven.
 *
 *   - `BinaryHitHold`   — classic on/off solenoid. The first apply pushes a
 *     fixed `(hit_ma, hold_ma, hit_time_ms)` profile via
 *     `ValveManager::ConfigureSolenoidCdr`; subsequent applies just toggle
 *     `EnableChannel` / `DisableChannel`.
 *
 *   - `VariableCurrent` — the same MAX22200 channel is used to drive a
 *     proportional valve. We push `hit = hold = target_current` with
 *     `hit_time = 0` (no hit phase) every time the requested position
 *     changes, giving 7-bit (~0.8 % of IFS) current resolution. This is
 *     coarse compared to the TLE92466ED but more than sufficient for
 *     valves like SEV that don't need true linear control.
 */
enum class SolenoidDriveMode : uint8_t {
    BinaryHitHold   = 0,
    VariableCurrent = 1,
};

constexpr const char* SolenoidDriveModeToString(SolenoidDriveMode m) noexcept {
    switch (m) {
        case SolenoidDriveMode::BinaryHitHold:   return "BinaryHitHold";
        case SolenoidDriveMode::VariableCurrent: return "VariableCurrent";
    }
    return "?";
}

//==============================================================================
// PER-CHANNEL DIAGNOSTIC FEATURE GATES
//==============================================================================

/**
 * @brief Which MAX22200 per-channel diagnostic features should be enabled.
 *
 * These map onto the MAX22200's per-channel CFG_CH register bits:
 *   - SRC  — Slew Rate Control   (reduces EMI, only valid when fCHOP < 50 kHz)
 *   - OL_EN — Open-Load detection
 *   - DPM_EN — Detection of Plunger Movement (low-side only). When the
 *     plunger doesn't move under load, MAX22200 raises a DPM fault — a
 *     critical safety signal in medical applications because it indicates
 *     a stuck or absent valve mechanism.
 *   - HHF_EN — HIT current check (HHF fault if IHIT not reached in tHIT;
 *     only meaningful for `BinaryHitHold`)
 *
 * Defaults are chosen for medical-device behaviour: open-load detection
 * is always on, slew-rate control is on (cleaner edges), HHF check is
 * on for hit-and-hold solenoids, and DPM is on for binary solenoids
 * with a known mechanical signature.
 */
struct Max22200ChannelFeatures {
    bool slew_rate_control{true};
    bool open_load_detection{true};
    bool plunger_movement_detection{false};  ///< Opt-in: requires DPM CFG tuned for this valve.
    bool hit_current_check{true};
};

/**
 * @brief Which TLE92466ED per-channel diagnostic features should be enabled.
 *
 * Maps onto fields of `tle92466ed::ChannelConfig`:
 *   - `slew_rate`            — output slew rate (default MEDIUM_2V5_US)
 *   - `diag_current`         — OFF-state diagnostic current
 *   - `open_load_threshold`  — 0 disables OL detection; 3 ≈ 3/8 of IFS
 *   - `auto_limit_disabled`  — leave false to use built-in current limit
 *   - `olsg_warning_enabled` — OLSG = Open-Load OR Short-to-Ground warning
 *   - `deep_dither_enabled`  — high-amplitude dither for sticky valves
 */
struct Tle92466ChannelFeatures {
    uint8_t open_load_threshold{3};       ///< 0=disabled, 1..7 = 1/8..7/8 of IFS
    bool    olsg_warning_enabled{true};
    bool    auto_limit_disabled{false};
    bool    deep_dither_enabled{false};
};

//==============================================================================
// MODEL DATABASE
//==============================================================================

/// Parker solenoid models we support.
enum class SolenoidModelId : uint8_t { C15, C21, VSO_PseudoProp };

/// Parker proportional valve families we support.
enum class ProportionalModelId : uint8_t {
    VSO,        ///< VSO/MD-PRO/HF-PRO/Lone-Wolf — solenoid actuated
    VSO_LowPro, ///< VSO LowPro / LowPro GC — solenoid actuated, lower power
    LM_Pro      ///< LM-Pro — linear-motor actuated, requires ≥ 5 kHz PWM
};

/**
 * @brief Datasheet-driven solenoid model record.
 *
 * Carries everything the SolenoidValve needs to push into the MAX22200:
 *   - Hit / hold currents (BinaryHitHold) computed from the rated voltage
 *     and coil resistance per the datasheet's Hit-and-Hold table.
 *   - Full-flow current (VariableCurrent) — the maximum current the
 *     proportional load is rated for at the chosen supply voltage. The
 *     SolenoidValve linearly maps `position 0..1 → 0..full_flow_ma`.
 *   - Per-channel diagnostic feature gates.
 */
struct SolenoidModelSpec {
    SolenoidModelId id;
    std::string_view label;
    /// Datasheet rated coil voltage (V). 24 V used on the WH bench.
    float           rated_voltage_v;
    /// Coil resistance at 20 °C for the rated_voltage_v variant (Ω).
    float           coil_resistance_ohm;
    /// Full hit current in mA (= V / R × 1000) — used in BinaryHitHold mode.
    uint16_t        hit_current_ma;
    /// Recommended hold current ≈ 50 % of hit (per datasheet) — BinaryHitHold mode.
    uint16_t        hold_current_ma;
    /// Maximum / full-flow current when driven proportionally (VariableCurrent mode).
    uint16_t        full_flow_current_ma;
    /// Minimum hit duration before switching to hold (ms) — BinaryHitHold only.
    float           min_hit_time_ms;
    /// Recommended PWM frequency floor for the hold phase (Hz) — informational.
    uint32_t        min_pwm_hz;
    /// Approximate response time published by the datasheet (ms).
    uint8_t         typ_response_ms;
};

/// Parker C21 specs at 24 V (datasheet Table 1 + Hit-and-Hold table).
inline constexpr SolenoidModelSpec kSpecC21_24V {
    .id                   = SolenoidModelId::C21,
    .label                = "Parker C21 (24V)",
    .rated_voltage_v      = 24.0f,
    .coil_resistance_ohm  = 235.0f,
    .hit_current_ma       = 102,            // 24 V / 235 Ω ≈ 102 mA
    .hold_current_ma      =  51,            // 50 % of hit per datasheet
    .full_flow_current_ma = 102,            // unused in BinaryHitHold; matches hit
    .min_hit_time_ms      = 100.0f,         // C21 Hit-and-Hold table
    .min_pwm_hz           = 1000,           // C21 PWM floor 1 kHz
    .typ_response_ms      = 10,
};

/// Parker C15 specs at 24 V — small-orifice (0.020"). Larger orifices use the
/// other coil resistance row; copy this struct and adjust if you stock those.
inline constexpr SolenoidModelSpec kSpecC15_24V_Small {
    .id                   = SolenoidModelId::C15,
    .label                = "Parker C15 0.020\" (24V)",
    .rated_voltage_v      = 24.0f,
    .coil_resistance_ohm  = 525.0f,
    .hit_current_ma       =  46,            // 24 V / 525 Ω ≈ 46 mA
    .hold_current_ma      =  23,
    .full_flow_current_ma =  46,
    .min_hit_time_ms      = 100.0f,
    .min_pwm_hz           = 1000,
    .typ_response_ms      = 10,
};

/// Parker C15 specs at 24 V for the larger orifices (0.040/0.060/0.080").
inline constexpr SolenoidModelSpec kSpecC15_24V_Large {
    .id                   = SolenoidModelId::C15,
    .label                = "Parker C15 ≥0.040\" (24V)",
    .rated_voltage_v      = 24.0f,
    .coil_resistance_ohm  = 361.0f,
    .hit_current_ma       =  66,            // 24 V / 361 Ω ≈ 66 mA
    .hold_current_ma      =  33,
    .full_flow_current_ma =  66,
    .min_hit_time_ms      = 100.0f,
    .min_pwm_hz           = 1000,
    .typ_response_ms      = 10,
};

/**
 * @brief VSO 24 V proportional driven via MAX22200 CDR (pseudo-proportional).
 *
 * Coil 274 Ω → ~66 mA at full open per the VSO Table 2. Used for SEV which
 * only needs coarse opening-percentage control (we exchange precision for
 * a freed-up TLE92466ED channel).
 */
inline constexpr SolenoidModelSpec kSpecVSO_24V_PseudoProp {
    .id                   = SolenoidModelId::VSO_PseudoProp,
    .label                = "Parker VSO (24V) — MAX22200 pseudo-prop",
    .rated_voltage_v      = 24.0f,
    .coil_resistance_ohm  = 274.0f,
    .hit_current_ma       = 0,             // unused in VariableCurrent
    .hold_current_ma      = 0,             // unused in VariableCurrent
    .full_flow_current_ma = 66,            // mA at 100 % open
    .min_hit_time_ms      = 0.0f,          // no hit phase in VariableCurrent
    .min_pwm_hz           = 5000,
    .typ_response_ms      = 10,
};

/**
 * @brief Datasheet-driven proportional model record (TLE92466ED).
 *
 * The mapping `position 0..1 → current` uses these limits:
 *   - Position 0   → 0 mA (valve fully closed for NC types).
 *   - Position 1   → `full_flow_current_ma`.
 *
 * `dither_*` are optional and only useful for VSO-class proportionals to
 * reduce stiction; the LM-Pro is a linear-motor design and does not benefit
 * from dither.
 */
struct ProportionalModelSpec {
    ProportionalModelId id;
    std::string_view    label;
    float               rated_voltage_v;
    float               coil_resistance_ohm;     ///< 0 for current-only (LM-Pro).
    uint16_t            full_flow_current_ma;    ///< Current required for Pmax flow.
    uint32_t            pwm_hz;                  ///< Recommended PWM frequency.
    uint16_t            dither_amplitude_ma;     ///< 0 disables dither.
    uint16_t            dither_frequency_hz;     ///< 0 disables dither.
    bool                supports_dither;         ///< Whether dither is helpful.
};

/// VSO 24 V coil — Table 2 row 5: 274 Ω → 66 mA full flow.
inline constexpr ProportionalModelSpec kSpecVSO_24V {
    .id                   = ProportionalModelId::VSO,
    .label                = "Parker VSO (24V)",
    .rated_voltage_v      = 24.0f,
    .coil_resistance_ohm  = 274.0f,
    .full_flow_current_ma = 66,
    .pwm_hz               = 8000,
    .dither_amplitude_ma  = 4,
    .dither_frequency_hz  = 200,
    .supports_dither      = true,
};

/// VSO LowPro 24 V coil — Table 2: 179 Ω, 65–92 mA full flow depending on
/// orifice (3 → 65, 4/5 → 75, 8 → 92). Pick the conservative middle value;
/// override per-valve if needed.
inline constexpr ProportionalModelSpec kSpecVSOLowPro_24V {
    .id                   = ProportionalModelId::VSO_LowPro,
    .label                = "Parker VSO LowPro (24V)",
    .rated_voltage_v      = 24.0f,
    .coil_resistance_ohm  = 179.0f,
    .full_flow_current_ma = 75,
    .pwm_hz               = 10000,
    .dither_amplitude_ma  = 4,
    .dither_frequency_hz  = 200,
    .supports_dither      = true,
};

/// LM-Pro 24 V coil — Table 1: 148 Ω, 115 mA full flow.
inline constexpr ProportionalModelSpec kSpecLMPro_24V {
    .id                   = ProportionalModelId::LM_Pro,
    .label                = "Parker LM-Pro (24V)",
    .rated_voltage_v      = 24.0f,
    .coil_resistance_ohm  = 148.0f,
    .full_flow_current_ma = 115,
    .pwm_hz               = 5000,           // LM-Pro datasheet: ≥ 5 kHz
    .dither_amplitude_ma  = 0,
    .dither_frequency_hz  = 0,
    .supports_dither      = false,
};

//==============================================================================
// BENCH MAPPING TABLES
//==============================================================================

/**
 * @brief Mapping row associating a named solenoid with a Flux driver channel,
 *        a datasheet model, a drive mode and per-channel diagnostic gates.
 */
struct SolenoidMapEntry {
    SolenoidId               id;
    std::string_view         label;
    ValveChannel             manager_channel;     ///< Flux::ValveManager channel.
    SolenoidModelSpec        model;
    SolenoidDriveMode        drive_mode;
    Max22200ChannelFeatures  features;
};

/**
 * @brief Mapping row associating a named proportional with a Flux driver
 *        channel and per-channel diagnostic gates.
 */
struct ProportionalMapEntry {
    ProportionalId            id;
    std::string_view          label;
    ValveChannel              manager_channel;
    ProportionalModelSpec     model;
    Tle92466ChannelFeatures   features;
};

/// Compile-time map of solenoid IDs → MAX22200 channels (6 entries → SOL_CH0..SOL_CH5).
inline constexpr std::array<SolenoidMapEntry, kNumSolenoids> kSolenoidMap {{
    { SolenoidId::OLV_1, "OLV_1", ValveChannel::SOL_CH0, kSpecC21_24V,
      SolenoidDriveMode::BinaryHitHold,
      { .slew_rate_control=true, .open_load_detection=true,
        .plunger_movement_detection=true, .hit_current_check=true } },

    { SolenoidId::OLV_2, "OLV_2", ValveChannel::SOL_CH1, kSpecC21_24V,
      SolenoidDriveMode::BinaryHitHold,
      { .slew_rate_control=true, .open_load_detection=true,
        .plunger_movement_detection=true, .hit_current_check=true } },

    { SolenoidId::VEV,   "VEV",   ValveChannel::SOL_CH2, kSpecC15_24V_Large,
      SolenoidDriveMode::BinaryHitHold,
      { .slew_rate_control=true, .open_load_detection=true,
        .plunger_movement_detection=true, .hit_current_check=true } },

    { SolenoidId::ORV,   "ORV",   ValveChannel::SOL_CH3, kSpecC15_24V_Large,
      SolenoidDriveMode::BinaryHitHold,
      { .slew_rate_control=true, .open_load_detection=true,
        .plunger_movement_detection=true, .hit_current_check=true } },

    { SolenoidId::GDV,   "GDV",   ValveChannel::SOL_CH4, kSpecC15_24V_Small,
      SolenoidDriveMode::BinaryHitHold,
      { .slew_rate_control=true, .open_load_detection=true,
        .plunger_movement_detection=true, .hit_current_check=true } },

    // SEV — VSO proportional driven proportionally via MAX22200 CDR (no
    // hit/hold). DPM is disabled because the current waveform is not the
    // same shape DPM was designed for; HHF is meaningless without a hit
    // phase, so we disable both.
    { SolenoidId::SEV,   "SEV",   ValveChannel::SOL_CH5, kSpecVSO_24V_PseudoProp,
      SolenoidDriveMode::VariableCurrent,
      { .slew_rate_control=true, .open_load_detection=true,
        .plunger_movement_detection=false, .hit_current_check=false } },
}};

/// Compile-time map of proportional IDs → TLE92466ED channels (PROP_CH0..PROP_CH5).
inline constexpr std::array<ProportionalMapEntry, kNumProportionals> kProportionalMap {{
    { ProportionalId::FCV,   "FCV",   ValveChannel::PROP_CH0, kSpecLMPro_24V,
      { .open_load_threshold=3, .olsg_warning_enabled=true } },
    { ProportionalId::FRV,   "FRV",   ValveChannel::PROP_CH1, kSpecLMPro_24V,
      { .open_load_threshold=3, .olsg_warning_enabled=true } },
    { ProportionalId::LSV,   "LSV",   ValveChannel::PROP_CH2, kSpecVSO_24V,
      { .open_load_threshold=3, .olsg_warning_enabled=true } },
    { ProportionalId::GFV,   "GFV",   ValveChannel::PROP_CH3, kSpecVSO_24V,
      { .open_load_threshold=3, .olsg_warning_enabled=true } },
    { ProportionalId::SPV_H, "SPV_H", ValveChannel::PROP_CH4, kSpecVSOLowPro_24V,
      { .open_load_threshold=3, .olsg_warning_enabled=true } },
    { ProportionalId::SPV_L, "SPV_L", ValveChannel::PROP_CH5, kSpecVSOLowPro_24V,
      { .open_load_threshold=3, .olsg_warning_enabled=true } },
}};

/// Convenience: O(1) accessor by enum.
inline constexpr const SolenoidMapEntry& MapOf(SolenoidId id) noexcept {
    return kSolenoidMap[static_cast<std::size_t>(id)];
}
inline constexpr const ProportionalMapEntry& MapOf(ProportionalId id) noexcept {
    return kProportionalMap[static_cast<std::size_t>(id)];
}

/// Compile-time enforcement: SEV must be in VariableCurrent mode (uses
/// MAX22200 CDR for proportional control). Tightens the contract so future
/// edits to the table can't silently break it.
static_assert(MapOf(SolenoidId::SEV).drive_mode == SolenoidDriveMode::VariableCurrent,
              "SEV is configured as a pseudo-proportional valve and must use VariableCurrent mode");

}  // namespace whvalve
