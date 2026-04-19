/**
 * @file ProportionalValve.h
 * @brief TLE92466ED-driven precision proportional valve.
 *
 * @details Wraps a Flux::ValveManager proportional channel (TLE92466ED
 *          backend) with the per-model PWM frequency, slew rate, optional
 *          dither and full-flow current pulled from `WhValveCatalog.h`.
 *
 *          Position semantics: 0 → 0 mA / valve closed (NC family);
 *          1 → `full_flow_current_ma`. The mapping is linear; control loops
 *          can perform any further linearisation in software.
 *
 *          On the very first apply the class pushes a complete
 *          `tle92466ed::ChannelConfig` (mode + open-load threshold + slew
 *          rate + dither) and enters mission mode through the handler;
 *          subsequent applies just update the current setpoint.
 *
 *          The diagnostics thread refreshes a per-channel `Snapshot()` that
 *          surfaces every fault flag, every warning, and the live measured
 *          current / duty-cycle / VBAT readings the chip publishes.
 *
 * @copyright HardFOC (internal use only)
 * @date 2026
 */

#pragma once

#include "Valve.h"
#include "WhValveCatalog.h"
#include "managers/ValveManager.h"
#include "core/hf-core-drivers/external/hf-tle92466ed-driver/inc/tle92466ed.hpp"
#include "handlers/tle92466ed/Tle92466edHandler.h"

#include <atomic>
#include <cmath>
#include <cstdint>

namespace whvalve {

class ProportionalValve final : public Valve {
public:
    ProportionalValve(const ProportionalMapEntry& entry) noexcept
        : Valve(entry.label), entry_(entry) {}

    ValveChannel ManagerChannel() const noexcept override { return entry_.manager_channel; }
    bool IsMapped() const noexcept override { return entry_.manager_channel != ValveChannel::CHANNEL_COUNT; }

    const ProportionalModelSpec& Model() const noexcept { return entry_.model; }
    const Tle92466ChannelFeatures& Features() const noexcept { return entry_.features; }

    /// Force a re-configuration of PWM/dither/features on the next Apply().
    void ForceReconfigure() noexcept {
        configured_.store(false, std::memory_order_release);
        features_pushed_.store(false, std::memory_order_release);
    }

    /**
     * @brief Convert a normalised position [0..1] into a current setpoint
     *        in milliamps using the configured `full_flow_current_ma`.
     */
    uint16_t PositionToCurrentMa(float pos) const noexcept {
        if (!std::isfinite(pos) || pos <= 0.0f) return 0;
        if (pos >= 1.0f) return entry_.model.full_flow_current_ma;
        return static_cast<uint16_t>(
            std::lroundf(pos * static_cast<float>(entry_.model.full_flow_current_ma)));
    }

    /// Lock-free per-channel diagnostic snapshot maintained by the
    /// diagnostics thread. Includes the live current / duty / VBAT readings.
    ProportionalDiagSnapshot Snapshot() const noexcept {
        ProportionalDiagSnapshot s{};
        const uint32_t bits = diag_bits_.load(std::memory_order_acquire);
        s.hardware_initialized        = (bits & kBitHwReady) != 0;
        s.overcurrent                 = (bits & kBitOc) != 0;
        s.short_to_ground             = (bits & kBitSg) != 0;
        s.open_load                   = (bits & kBitOl) != 0;
        s.over_temperature            = (bits & kBitOt) != 0;
        s.open_load_short_ground      = (bits & kBitOlSg) != 0;
        s.ot_warning                  = (bits & kBitOtWarn) != 0;
        s.current_regulation_warning  = (bits & kBitCurRegWarn) != 0;
        s.pwm_regulation_warning      = (bits & kBitPwmRegWarn) != 0;
        s.olsg_warning                = (bits & kBitOlsgWarn) != 0;
        s.average_current_raw         = avg_current_raw_.load(std::memory_order_acquire);
        s.duty_cycle_raw              = duty_cycle_raw_.load(std::memory_order_acquire);
        s.vbat_feedback_raw           = vbat_feedback_raw_.load(std::memory_order_acquire);
        s.fault_count                 = GetFaultCount();
        s.last_fault_tick_ms          = GetLastFaultTickMs();
        return s;
    }

    //----------------------------------------------------------------------
    // Manager-internal API (control thread)
    //----------------------------------------------------------------------

    hf_valve_err_t ApplyChannelFeatures(ValveManager& mgr) noexcept override {
        if (!IsMapped()) return hf_valve_err_t::VALVE_ERR_DRIVER_NOT_AVAILABLE;
        if (features_pushed_.load(std::memory_order_acquire)) {
            return hf_valve_err_t::VALVE_SUCCESS;
        }

        Tle92466edHandler* h = mgr.GetTle92466Handler();
        if (!h) return hf_valve_err_t::VALVE_ERR_DRIVER_NOT_AVAILABLE;

        const uint8_t hw = static_cast<uint8_t>(entry_.manager_channel) -
                           static_cast<uint8_t>(ValveChannel::PROP_CH0);

        // Build a full ChannelConfig from the model spec and feature gates.
        tle92466ed::ChannelConfig cfg{};
        cfg.mode                = tle92466ed::ChannelMode::ICC;
        cfg.current_setpoint_ma = 0;
        cfg.slew_rate           = tle92466ed::SlewRate::MEDIUM_2V5_US;
        cfg.diag_current        = tle92466ed::DiagCurrent::I_80UA;
        cfg.open_load_threshold = entry_.features.open_load_threshold;
        cfg.olsg_warning_enabled = entry_.features.olsg_warning_enabled;
        cfg.auto_limit_disabled  = entry_.features.auto_limit_disabled;
        cfg.deep_dither_enabled  = entry_.features.deep_dither_enabled;
        // Dither step / step-size / flat are filled by ValveManager when
        // ConfigureProportionalDither is called below; leave them zeroed here.

        const auto cfg_res = h->ConfigureChannel(hw, cfg);
        if (!cfg_res.has_value()) {
            return TleDriverErrorToValveError(cfg_res.error());
        }

        // Push PWM frequency through the manager (it converts Hz → mantissa+exp).
        const auto pwm_err = mgr.ConfigureProportionalPwm(
            entry_.manager_channel, static_cast<float>(entry_.model.pwm_hz));
        if (pwm_err != hf_valve_err_t::VALVE_SUCCESS) {
            return pwm_err;
        }

        // Optional dither (VSO-class only).
        if (entry_.model.supports_dither &&
            entry_.model.dither_amplitude_ma > 0 &&
            entry_.model.dither_frequency_hz > 0) {
            const auto d_err = mgr.ConfigureProportionalDither(
                entry_.manager_channel,
                static_cast<float>(entry_.model.dither_amplitude_ma),
                static_cast<float>(entry_.model.dither_frequency_hz));
            // Dither failure is non-fatal — keep going, just record the error.
            if (d_err != hf_valve_err_t::VALVE_SUCCESS) {
                RecordApplyResult(d_err, false);
            }
        }

        features_pushed_.store(true, std::memory_order_release);
        configured_.store(true, std::memory_order_release);
        return hf_valve_err_t::VALVE_SUCCESS;
    }

    hf_valve_err_t Apply(ValveManager& mgr) noexcept override {
        if (!IsMapped()) {
            const auto err = hf_valve_err_t::VALVE_ERR_DRIVER_NOT_AVAILABLE;
            SetState(ValveState::Uninitialized);
            RecordApplyResult(err, false);
            return err;
        }

        if (!features_pushed_.load(std::memory_order_acquire)) {
            const auto fe = ApplyChannelFeatures(mgr);
            if (fe != hf_valve_err_t::VALVE_SUCCESS) {
                RecordApplyResult(fe, false);
                return fe;
            }
        }

        const float pos = GetTargetPosition();
        const uint16_t cur_ma = PositionToCurrentMa(pos);

        if (cur_ma == 0) {
            const auto err = mgr.DisableChannel(entry_.manager_channel);
            const bool ok = (err == hf_valve_err_t::VALVE_SUCCESS);
            if (ok) {
                SetActualPosition(0.0f);
                SetState(ValveState::Idle);
            }
            RecordApplyResult(err, ok);
            return err;
        }

        const auto enable_err = mgr.EnableChannel(entry_.manager_channel);
        if (enable_err != hf_valve_err_t::VALVE_SUCCESS) {
            RecordApplyResult(enable_err, false);
            return enable_err;
        }

        const auto set_err = mgr.SetProportionalCurrent(entry_.manager_channel, cur_ma);
        const bool ok = (set_err == hf_valve_err_t::VALVE_SUCCESS);
        if (ok) {
            const float applied = static_cast<float>(cur_ma) /
                                  static_cast<float>(entry_.model.full_flow_current_ma);
            SetActualPosition(applied > 1.0f ? 1.0f : applied);
            SetState(ValveState::Active);
        }
        RecordApplyResult(set_err, ok);
        return set_err;
    }

    //----------------------------------------------------------------------
    // Diagnostics: TLE92466ED per-channel snapshot
    //----------------------------------------------------------------------

    hf_valve_err_t OnDiagnostics(ValveManager& mgr) noexcept override {
        if (!IsMapped()) {
            diag_bits_.store(0, std::memory_order_release);
            SetState(ValveState::Uninitialized);
            return hf_valve_err_t::VALVE_ERR_DRIVER_NOT_AVAILABLE;
        }

        const uint8_t hw = static_cast<uint8_t>(entry_.manager_channel) -
                           static_cast<uint8_t>(ValveChannel::PROP_CH0);

        // Per-channel rich diagnostics (current / duty / VBAT) come from the
        // handler's GetChannelDiagnostics, not from FaultReport. Use both.
        Tle92466edHandler* h = mgr.GetTle92466Handler();

        bool any_fault = false;
        uint32_t bits = kBitHwReady;

        if (h) {
            tle92466ed::ChannelDiagnostics ch_diag{};
            const auto dr = h->GetChannelDiagnostics(hw, ch_diag);
            if (dr.has_value()) {
                if (ch_diag.overcurrent)            { bits |= kBitOc;          any_fault = true; }
                if (ch_diag.short_to_ground)        { bits |= kBitSg;          any_fault = true; }
                if (ch_diag.open_load)              { bits |= kBitOl;          any_fault = true; }
                if (ch_diag.over_temperature)       { bits |= kBitOt;          any_fault = true; }
                if (ch_diag.open_load_short_ground) { bits |= kBitOlSg;        any_fault = true; }
                if (ch_diag.ot_warning)                 bits |= kBitOtWarn;
                if (ch_diag.current_regulation_warning) bits |= kBitCurRegWarn;
                if (ch_diag.pwm_regulation_warning)     bits |= kBitPwmRegWarn;
                if (ch_diag.olsg_warning)               bits |= kBitOlsgWarn;
                avg_current_raw_.store(ch_diag.average_current, std::memory_order_release);
                duty_cycle_raw_.store(ch_diag.duty_cycle,       std::memory_order_release);
                vbat_feedback_raw_.store(ch_diag.vbat_feedback, std::memory_order_release);
            } else {
                last_diag_err_ = TleDriverErrorToValveError(dr.error());
                diag_bits_.store(0, std::memory_order_release);
                return last_diag_err_;
            }
        } else {
            // Handler unavailable — fall back to FaultReport via ValveManager.
            tle92466ed::FaultReport report{};
            const hf_valve_err_t err = mgr.GetProportionalFaults(report);
            if (err != hf_valve_err_t::VALVE_SUCCESS) {
                diag_bits_.store(0, std::memory_order_release);
                last_diag_err_ = err;
                return err;
            }
            const auto& ch = report.channels[hw];
            if (ch.overcurrent)            { bits |= kBitOc;       any_fault = true; }
            if (ch.short_to_ground)        { bits |= kBitSg;       any_fault = true; }
            if (ch.open_load)              { bits |= kBitOl;       any_fault = true; }
            if (ch.over_temperature)       { bits |= kBitOt;       any_fault = true; }
            if (ch.open_load_short_ground) { bits |= kBitOlSg;     any_fault = true; }
            if (ch.ot_warning)                 bits |= kBitOtWarn;
            if (ch.current_regulation_warning) bits |= kBitCurRegWarn;
            if (ch.pwm_regulation_warning)     bits |= kBitPwmRegWarn;
            if (ch.olsg_warning)               bits |= kBitOlsgWarn;
        }

        diag_bits_.store(bits, std::memory_order_release);
        last_diag_err_ = hf_valve_err_t::VALVE_SUCCESS;

        if (any_fault) {
            const hf_valve_err_t agg = hf_valve_err_t::VALVE_ERR_HARDWARE_FAULT;
            RecordFault(agg, now_ms_);
            return agg;
        }
        return hf_valve_err_t::VALVE_SUCCESS;
    }

    void SetDiagnosticTimestamp(uint64_t now_ms) noexcept { now_ms_ = now_ms; }
    hf_valve_err_t GetLastDiagError() const noexcept { return last_diag_err_; }

private:
    static constexpr uint32_t kBitHwReady     = 1U << 0;
    static constexpr uint32_t kBitOc          = 1U << 1;
    static constexpr uint32_t kBitSg          = 1U << 2;
    static constexpr uint32_t kBitOl          = 1U << 3;
    static constexpr uint32_t kBitOt          = 1U << 4;
    static constexpr uint32_t kBitOlSg        = 1U << 5;
    static constexpr uint32_t kBitOtWarn      = 1U << 6;
    static constexpr uint32_t kBitCurRegWarn  = 1U << 7;
    static constexpr uint32_t kBitPwmRegWarn  = 1U << 8;
    static constexpr uint32_t kBitOlsgWarn    = 1U << 9;

    const ProportionalMapEntry& entry_;
    std::atomic<bool>           configured_{false};
    std::atomic<bool>           features_pushed_{false};
    std::atomic<uint32_t>       diag_bits_{0};
    std::atomic<uint16_t>       avg_current_raw_{0};
    std::atomic<uint16_t>       duty_cycle_raw_{0};
    std::atomic<uint16_t>       vbat_feedback_raw_{0};
    hf_valve_err_t              last_diag_err_{hf_valve_err_t::VALVE_SUCCESS};
    uint64_t                    now_ms_{0};
};

}  // namespace whvalve
