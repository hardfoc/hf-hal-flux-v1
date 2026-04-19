/**
 * @file SolenoidValve.h
 * @brief MAX22200-driven solenoid valve — supports both BinaryHitHold and
 *        VariableCurrent (pseudo-proportional) drive modes.
 *
 * @details Wraps a Flux::ValveManager solenoid channel with the C15/C21
 *          hit-and-hold profile (or, for SEV, the VSO-as-pseudo-proportional
 *          profile) pulled from `WhValveCatalog.h`.
 *
 *          Position semantics:
 *            - `BinaryHitHold`   — `pos > 0 → energise (hit then hold)`,
 *                                  `pos == 0 → de-energise`.
 *            - `VariableCurrent` — `pos ∈ [0,1] → current ∈
 *                                  [0, full_flow_current_ma]` written into
 *                                  both hit and hold setpoints with
 *                                  `hit_time = 0` so the channel runs at a
 *                                  steady current with no hit pulse.
 *
 *          Per-channel diagnostic features (open-load, plunger-movement,
 *          hit-current check, slew-rate control) are configured once via
 *          `ApplyChannelFeatures()` and surface through `Snapshot()`.
 *
 * @copyright HardFOC (internal use only)
 * @date 2026
 */

#pragma once

#include "Valve.h"
#include "WhValveCatalog.h"
#include "managers/ValveManager.h"
#include "core/hf-core-drivers/external/hf-max22200-driver/inc/max22200_types.hpp"
#include "handlers/max22200/Max22200Handler.h"

#include <atomic>
#include <cmath>
#include <cstdint>

namespace whvalve {

class SolenoidValve final : public Valve {
public:
    SolenoidValve(const SolenoidMapEntry& entry) noexcept
        : Valve(entry.label), entry_(entry) {}

    ValveChannel ManagerChannel() const noexcept override { return entry_.manager_channel; }

    /// Force a re-configuration of the CDR profile + channel features on the
    /// next Apply().
    void ForceReconfigure() noexcept {
        configured_.store(false, std::memory_order_release);
        features_pushed_.store(false, std::memory_order_release);
    }

    const SolenoidModelSpec& Model() const noexcept { return entry_.model; }
    SolenoidDriveMode DriveMode() const noexcept { return entry_.drive_mode; }
    const Max22200ChannelFeatures& Features() const noexcept { return entry_.features; }

    /// Linearly map normalised position [0..1] to a current setpoint in mA
    /// using `full_flow_current_ma` (used in VariableCurrent mode).
    uint16_t PositionToCurrentMa(float pos) const noexcept {
        if (!std::isfinite(pos) || pos <= 0.0f) return 0;
        if (pos >= 1.0f) return entry_.model.full_flow_current_ma;
        return static_cast<uint16_t>(
            std::lroundf(pos * static_cast<float>(entry_.model.full_flow_current_ma)));
    }

    /// Lock-free per-channel diagnostic snapshot (updated by diagnostics thread).
    SolenoidDiagSnapshot Snapshot() const noexcept {
        SolenoidDiagSnapshot s{};
        const uint32_t bits = diag_bits_.load(std::memory_order_acquire);
        s.hardware_initialized   = (bits & kBitHwReady) != 0;
        s.overcurrent            = (bits & kBitOcp) != 0;
        s.hit_not_reached        = (bits & kBitHhf) != 0;
        s.open_load              = (bits & kBitOlf) != 0;
        s.plunger_movement_fault = (bits & kBitDpm) != 0;
        s.fault_count            = GetFaultCount();
        s.last_fault_tick_ms     = GetLastFaultTickMs();
        return s;
    }

    //----------------------------------------------------------------------
    // Manager-internal API (control thread)
    //----------------------------------------------------------------------

    hf_valve_err_t ApplyChannelFeatures(ValveManager& mgr) noexcept override {
        if (features_pushed_.load(std::memory_order_acquire)) {
            return hf_valve_err_t::VALVE_SUCCESS;
        }
        // The Flux::ValveManager doesn't expose per-feature setters yet,
        // so reach through to the underlying handler. The handler caches
        // its own ChannelConfig; fetching, mutating, and re-configuring is
        // the safe round-trip.
        Max22200Handler* h = mgr.GetMax22200Handler();
        if (!h) return hf_valve_err_t::VALVE_ERR_DRIVER_NOT_AVAILABLE;

        const uint8_t hw = static_cast<uint8_t>(entry_.manager_channel) -
                           static_cast<uint8_t>(ValveChannel::SOL_CH0);

        // Build a fresh ChannelConfig from the model + per-channel feature
        // gates. Drive mode is always CDR (current); side mode LOW_SIDE.
        max22200::ChannelConfig cfg = (entry_.drive_mode == SolenoidDriveMode::BinaryHitHold)
            ? max22200::ChannelConfig::makeSolenoidCdr(
                  static_cast<float>(entry_.model.hit_current_ma),
                  static_cast<float>(entry_.model.hold_current_ma),
                  entry_.model.min_hit_time_ms)
            : max22200::ChannelConfig::makeSolenoidCdr(
                  /*hit_ma  =*/ static_cast<float>(PositionToCurrentMa(GetTargetPosition())),
                  /*hold_ma =*/ static_cast<float>(PositionToCurrentMa(GetTargetPosition())),
                  /*hit_time=*/ 0.0f);

        cfg.slew_rate_control_enabled        = entry_.features.slew_rate_control;
        cfg.open_load_detection_enabled      = entry_.features.open_load_detection;
        cfg.plunger_movement_detection_enabled = entry_.features.plunger_movement_detection;
        cfg.hit_current_check_enabled        = entry_.features.hit_current_check;

        const auto err = MaxDriverStatusToValveError(h->ConfigureChannel(hw, cfg));
        if (err == hf_valve_err_t::VALVE_SUCCESS) {
            features_pushed_.store(true, std::memory_order_release);
            configured_.store(true, std::memory_order_release);
        }
        return err;
    }

    hf_valve_err_t Apply(ValveManager& mgr) noexcept override {
        // Push channel features (and base CDR profile) on the very first
        // apply, or after ForceReconfigure() / a fault recovery flow.
        if (!features_pushed_.load(std::memory_order_acquire)) {
            const auto fe = ApplyChannelFeatures(mgr);
            if (fe != hf_valve_err_t::VALVE_SUCCESS) {
                RecordApplyResult(fe, false);
                return fe;
            }
        }

        const float target = GetTargetPosition();

        if (entry_.drive_mode == SolenoidDriveMode::BinaryHitHold) {
            const bool want_on = (target > 0.0f);
            const hf_valve_err_t err = want_on
                ? mgr.EnableChannel(entry_.manager_channel)
                : mgr.DisableChannel(entry_.manager_channel);
            const bool ok = (err == hf_valve_err_t::VALVE_SUCCESS);
            if (ok) {
                SetActualPosition(want_on ? 1.0f : 0.0f);
                SetState(want_on ? ValveState::Active : ValveState::Disabled);
            }
            RecordApplyResult(err, ok);
            return err;
        }

        // ── VariableCurrent: re-issue ConfigureSolenoidCdr each apply ─────
        const uint16_t target_ma = PositionToCurrentMa(target);
        if (target_ma == 0) {
            const auto err = mgr.DisableChannel(entry_.manager_channel);
            const bool ok = (err == hf_valve_err_t::VALVE_SUCCESS);
            if (ok) {
                SetActualPosition(0.0f);
                SetState(ValveState::Idle);
            }
            RecordApplyResult(err, ok);
            return err;
        }

        // Push fresh CDR setpoints for this iteration.
        const auto cfg_err = mgr.ConfigureSolenoidCdr(
            entry_.manager_channel, target_ma, target_ma, /*hit_time_ms=*/0.0f);
        if (cfg_err != hf_valve_err_t::VALVE_SUCCESS) {
            RecordApplyResult(cfg_err, false);
            return cfg_err;
        }

        const auto en_err = mgr.EnableChannel(entry_.manager_channel);
        const bool ok = (en_err == hf_valve_err_t::VALVE_SUCCESS);
        if (ok) {
            const float applied = static_cast<float>(target_ma) /
                                  static_cast<float>(entry_.model.full_flow_current_ma);
            SetActualPosition(applied > 1.0f ? 1.0f : applied);
            SetState(ValveState::Active);
        }
        RecordApplyResult(en_err, ok);
        return en_err;
    }

    //----------------------------------------------------------------------
    // Diagnostics: MAX22200 per-channel fault decoding
    //----------------------------------------------------------------------

    hf_valve_err_t OnDiagnostics(ValveManager& mgr) noexcept override {
        max22200::FaultStatus faults{};
        const hf_valve_err_t err = mgr.GetSolenoidFaults(faults);
        if (err != hf_valve_err_t::VALVE_SUCCESS) {
            // Driver down; drop the cached snapshot to "uninitialized" so
            // the application can react.
            diag_bits_.store(0, std::memory_order_release);
            last_diag_err_ = err;
            return err;
        }
        last_diag_err_ = hf_valve_err_t::VALVE_SUCCESS;

        const auto hw = static_cast<uint8_t>(entry_.manager_channel) -
                        static_cast<uint8_t>(ValveChannel::SOL_CH0);
        const uint8_t ch_bit = static_cast<uint8_t>(1U << hw);

        const bool ocp = (faults.overcurrent_channel_mask           & ch_bit) != 0;
        const bool hhf = (faults.hit_not_reached_channel_mask       & ch_bit) != 0;
        const bool olf = (faults.open_load_fault_channel_mask       & ch_bit) != 0;
        const bool dpm = (faults.plunger_movement_fault_channel_mask & ch_bit) != 0;

        uint32_t bits = kBitHwReady;
        if (ocp) bits |= kBitOcp;
        if (hhf) bits |= kBitHhf;
        if (olf) bits |= kBitOlf;
        if (dpm) bits |= kBitDpm;
        diag_bits_.store(bits, std::memory_order_release);

        if (ocp || hhf || olf || dpm) {
            const hf_valve_err_t agg = hf_valve_err_t::VALVE_ERR_HARDWARE_FAULT;
            RecordFault(agg, now_ms_);
            return agg;
        }
        return hf_valve_err_t::VALVE_SUCCESS;
    }

    void SetDiagnosticTimestamp(uint64_t now_ms) noexcept { now_ms_ = now_ms; }
    hf_valve_err_t GetLastDiagError() const noexcept { return last_diag_err_; }

private:
    static constexpr uint32_t kBitHwReady = 1U << 0;
    static constexpr uint32_t kBitOcp     = 1U << 1;
    static constexpr uint32_t kBitHhf     = 1U << 2;
    static constexpr uint32_t kBitOlf     = 1U << 3;
    static constexpr uint32_t kBitDpm     = 1U << 4;

    const SolenoidMapEntry&  entry_;
    std::atomic<bool>        configured_{false};
    std::atomic<bool>        features_pushed_{false};
    std::atomic<uint32_t>    diag_bits_{0};      ///< Packed per-channel diag flags.
    hf_valve_err_t           last_diag_err_{hf_valve_err_t::VALVE_SUCCESS};
    uint64_t                 now_ms_{0};
};

}  // namespace whvalve
