/**
 * @file WhValveCatalog.h
 * @brief Compile-time catalog of all WH bench valves and their datasheet specs.
 *
 * @details This header is the single source of truth for the bench valve set:
 *          - Two named ID enums (one per valve family) that drive O(1) lookup.
 *          - A model database for the Parker valves we drive (C21/C15 solenoids,
 *            VSO/LM-Pro proportionals) with the electrical numbers pulled
 *            directly from each datasheet. Adding/removing a model is a single
 *            literal change here; nothing else in the system changes.
 *          - Two compile-time mapping tables (`kSolenoidMap`, `kProportionalMap`)
 *            assigning each named valve to a Flux::ValveChannel and a model.
 *
 * @par Hardware references
 *   - Honeywell SSC TruStability — sensor side (see PressureSensorConfig.h).
 *   - Parker C21 cartridge solenoid (Hit-and-Hold §"Optional Reduced Power
 *     Control Method", 2.5 W full hit, 50 % hold, ≥ 1 kHz PWM).
 *   - Parker C15 cartridge solenoid (Hit-and-Hold §"Optional Reduced Power
 *     Control Method", 1.1–1.7 W full hit, 50 % hold, ≥ 1 kHz PWM).
 *   - Parker VSO TruStability proportional (Table 2, current-controlled, 5–12 kHz PWM).
 *   - Parker LM-Pro linear-motor proportional (Table 1, current-controlled,
 *     5 kHz PWM minimum).
 *
 * @par Driver IC mapping (Flux V1 board)
 *   - Solenoids → MAX22200 (Flux::ValveManager SOL_CH0..SOL_CH7).
 *     Used in CDR mode with hit/hold currents per the C21/C15 datasheets.
 *   - Proportionals → TLE92466ED (Flux::ValveManager PROP_CH0..PROP_CH5).
 *     Used in current-controlled PWM mode with full-flow current pulled from
 *     the relevant VSO/LM-Pro electrical table.
 *
 * @warning TLE92466ED has only 6 channels. The bench currently lists 7
 *          proportional valves; SPV_L is therefore declared `Unmapped`.
 *          Either drop one valve or add a second TLE92466ED IC and assign
 *          it to that slot.
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
 * @brief WH bench solenoid valves.
 *
 * @note Adding a new solenoid:
 *       1. Add an enumerator above `Count` here.
 *       2. Add a row in `kSolenoidMap` (below) referencing a `SolenoidModelId`.
 *       That is the entire change; storage, threads, diagnostics, and the
 *       lookup APIs scale automatically.
 */
enum class SolenoidId : uint8_t {
    OLV_1 = 0,   ///< Outlet valve #1
    OLV_2,       ///< Outlet valve #2
    VEV,         ///< Vent valve
    ORV,         ///< Overflow / outflow valve
    GDV,         ///< Gas/drain/discharge valve
    Count        ///< Sentinel — keep last.
};

/**
 * @brief WH bench proportional valves.
 *
 * @note Same extension procedure as SolenoidId; add an enumerator and a row
 *       in `kProportionalMap`.
 */
enum class ProportionalId : uint8_t {
    FCV   = 0,   ///< Flow control valve
    FRV,         ///< Flow regulator valve
    LSV,         ///< Load-sensing valve
    SEV,         ///< Selector valve
    GFV,         ///< Gas flow valve
    SPV_H,       ///< Supply pressure valve — high range
    SPV_L,       ///< Supply pressure valve — low range  (currently `Unmapped`)
    Count        ///< Sentinel — keep last.
};

/// Compile-time count helpers.
inline constexpr std::size_t kNumSolenoids     = static_cast<std::size_t>(SolenoidId::Count);
inline constexpr std::size_t kNumProportionals = static_cast<std::size_t>(ProportionalId::Count);

//==============================================================================
// MODEL DATABASE
//==============================================================================

/// Parker solenoid models we support.
enum class SolenoidModelId : uint8_t { C15, C21 };

/// Parker proportional valve families we support.
enum class ProportionalModelId : uint8_t {
    VSO,        ///< VSO/MD-PRO/HF-PRO/Lone-Wolf — solenoid actuated
    VSO_LowPro, ///< VSO LowPro / LowPro GC — solenoid actuated, lower power
    LM_Pro      ///< LM-Pro — linear-motor actuated, requires ≥ 5 kHz PWM
};

/**
 * @brief Datasheet-driven solenoid model record.
 *
 * Every value is pulled directly from the C15 / C21 datasheets so that the
 * pre-computed hit/hold currents respect the manufacturer's recommendations:
 *   - Hit (spike) at rated voltage for ≥ `min_hit_time_ms`,
 *   - Hold at 50 % of rated voltage (i.e. roughly 50 % hit current),
 *   - PWM ≥ `min_pwm_hz`.
 *
 * The ValveManager backend is configured in CDR (current drive regulation)
 * mode using `hit_current_ma` for inrush and `hold_current_ma` for sustain.
 */
struct SolenoidModelSpec {
    SolenoidModelId id;
    std::string_view label;
    /// Datasheet rated coil voltage (V). 24 V used on the WH bench.
    float           rated_voltage_v;
    /// Coil resistance at 20 °C for the rated_voltage_v variant (Ω).
    float           coil_resistance_ohm;
    /// Full hit current in mA = V / R × 1000.
    uint16_t        hit_current_ma;
    /// Recommended hold current ≈ 50 % of hit (per datasheet).
    uint16_t        hold_current_ma;
    /// Minimum hit duration before switching to hold (ms).
    float           min_hit_time_ms;
    /// Recommended PWM frequency floor for the hold phase (Hz).
    uint32_t        min_pwm_hz;
    /// Approximate response time published by the datasheet (ms).
    uint8_t         typ_response_ms;
};

/// Parker C21 specs at 24 V (datasheet Table 1 + Hit-and-Hold table).
inline constexpr SolenoidModelSpec kSpecC21_24V {
    .id                  = SolenoidModelId::C21,
    .label               = "Parker C21 (24V)",
    .rated_voltage_v     = 24.0f,
    .coil_resistance_ohm = 235.0f,
    .hit_current_ma      = 102,            // 24 V / 235 Ω ≈ 102 mA
    .hold_current_ma     =  51,            // 50 % of hit per datasheet
    .min_hit_time_ms     = 100.0f,         // C21 Hit-and-Hold table
    .min_pwm_hz          = 1000,           // C21 PWM floor 1 kHz
    .typ_response_ms     = 10,
};

/// Parker C15 specs at 24 V — small-orifice (0.020"). Larger orifices use the
/// other coil resistance row; copy this struct and adjust if you stock those.
inline constexpr SolenoidModelSpec kSpecC15_24V_Small {
    .id                  = SolenoidModelId::C15,
    .label               = "Parker C15 0.020\" (24V)",
    .rated_voltage_v     = 24.0f,
    .coil_resistance_ohm = 525.0f,
    .hit_current_ma      =  46,            // 24 V / 525 Ω ≈ 46 mA
    .hold_current_ma     =  23,
    .min_hit_time_ms     = 100.0f,
    .min_pwm_hz          = 1000,
    .typ_response_ms     = 10,
};

/// Parker C15 specs at 24 V for the larger orifices (0.040/0.060/0.080").
inline constexpr SolenoidModelSpec kSpecC15_24V_Large {
    .id                  = SolenoidModelId::C15,
    .label               = "Parker C15 ≥0.040\" (24V)",
    .rated_voltage_v     = 24.0f,
    .coil_resistance_ohm = 361.0f,
    .hit_current_ma      =  66,            // 24 V / 361 Ω ≈ 66 mA
    .hold_current_ma     =  33,
    .min_hit_time_ms     = 100.0f,
    .min_pwm_hz          = 1000,
    .typ_response_ms     = 10,
};

/**
 * @brief Datasheet-driven proportional model record.
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
/// orifice (3 → 65, 4/5 → 75, 8 → 92).  Pick the conservative middle value;
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
// BENCH MAPPING TABLES (SolenoidId / ProportionalId → driver channel + spec)
//==============================================================================

/**
 * @brief Mapping row associating a named solenoid with a Flux driver channel
 *        and its datasheet model.
 *
 * `manager_channel` resolves to one of the MAX22200 SOL_CHn rows in
 * Flux::ValveManager.
 */
struct SolenoidMapEntry {
    SolenoidId        id;
    std::string_view  label;
    ValveChannel      manager_channel;       ///< Flux::ValveManager channel.
    SolenoidModelSpec model;
};

/**
 * @brief Mapping row associating a named proportional with a Flux driver
 *        channel (or `ValveChannel::CHANNEL_COUNT` if currently unmapped).
 */
struct ProportionalMapEntry {
    ProportionalId      id;
    std::string_view    label;
    ValveChannel        manager_channel;     ///< CHANNEL_COUNT == unmapped.
    ProportionalModelSpec model;
};

/// Compile-time map of solenoid IDs → MAX22200 channels.
inline constexpr std::array<SolenoidMapEntry, kNumSolenoids> kSolenoidMap {{
    { SolenoidId::OLV_1, "OLV_1", ValveChannel::SOL_CH0, kSpecC21_24V        },
    { SolenoidId::OLV_2, "OLV_2", ValveChannel::SOL_CH1, kSpecC21_24V        },
    { SolenoidId::VEV,   "VEV",   ValveChannel::SOL_CH2, kSpecC15_24V_Large  },
    { SolenoidId::ORV,   "ORV",   ValveChannel::SOL_CH3, kSpecC15_24V_Large  },
    { SolenoidId::GDV,   "GDV",   ValveChannel::SOL_CH4, kSpecC15_24V_Small  },
}};

/// Compile-time map of proportional IDs → TLE92466ED channels (with one
/// unmapped slot — see file header warning).
inline constexpr std::array<ProportionalMapEntry, kNumProportionals> kProportionalMap {{
    { ProportionalId::FCV,   "FCV",   ValveChannel::PROP_CH0,        kSpecLMPro_24V      },
    { ProportionalId::FRV,   "FRV",   ValveChannel::PROP_CH1,        kSpecLMPro_24V      },
    { ProportionalId::LSV,   "LSV",   ValveChannel::PROP_CH2,        kSpecVSO_24V        },
    { ProportionalId::SEV,   "SEV",   ValveChannel::PROP_CH3,        kSpecVSO_24V        },
    { ProportionalId::GFV,   "GFV",   ValveChannel::PROP_CH4,        kSpecVSO_24V        },
    { ProportionalId::SPV_H, "SPV_H", ValveChannel::PROP_CH5,        kSpecVSOLowPro_24V  },
    // No 7th channel on TLE92466ED — see header warning. Replace
    // CHANNEL_COUNT with a real channel once a second IC is added.
    { ProportionalId::SPV_L, "SPV_L", ValveChannel::CHANNEL_COUNT,   kSpecVSOLowPro_24V  },
}};

/// Convenience: O(1) accessor by enum.
inline constexpr const SolenoidMapEntry& MapOf(SolenoidId id) noexcept {
    return kSolenoidMap[static_cast<std::size_t>(id)];
}
inline constexpr const ProportionalMapEntry& MapOf(ProportionalId id) noexcept {
    return kProportionalMap[static_cast<std::size_t>(id)];
}

inline constexpr bool IsMapped(const ProportionalMapEntry& e) noexcept {
    return e.manager_channel != ValveChannel::CHANNEL_COUNT;
}

}  // namespace whvalve
