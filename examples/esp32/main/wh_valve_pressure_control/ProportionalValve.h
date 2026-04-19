/**
 * @file ProportionalValve.h
 * @brief Concrete valve type for Parker VSO / VSO LowPro / LM-Pro proportionals.
 *
 * @details Wraps a Flux::ValveManager proportional channel (TLE92466ED backend)
 *          with the per-model PWM frequency, optional dither and full-flow
 *          current pulled from `WhValveCatalog.h`.
 *
 *          Position semantics: 0 → 0 mA / valve closed (NC family);
 *          1 → `full_flow_current_ma`. The mapping is linear; control loops
 *          can perform any further linearisation in software.
 *
 *          The TLE92466ED is configured **once** per session (PWM frequency,
 *          dither). Subsequent applies just push the new current setpoint.
 *
 * @copyright HardFOC (internal use only)
 * @date 2026
 */

#pragma once

#include "Valve.h"
#include "WhValveCatalog.h"
#include "managers/ValveManager.h"

#include <cmath>

namespace whvalve {

class ProportionalValve final : public Valve {
public:
    ProportionalValve(const ProportionalMapEntry& entry) noexcept
        : Valve(entry.label), entry_(entry) {}

    ValveChannel ManagerChannel() const noexcept override { return entry_.manager_channel; }
    bool IsMapped() const noexcept override { return whvalve::IsMapped(entry_); }

    const ProportionalModelSpec& Model() const noexcept { return entry_.model; }

    /// Force a re-configuration of PWM/dither on the next Apply() call.
    void ForceReconfigure() noexcept { configured_.store(false, std::memory_order_release); }

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

    //----------------------------------------------------------------------
    // Manager-internal API (control thread)
    //----------------------------------------------------------------------

    hf_valve_err_t Apply(ValveManager& mgr) noexcept override {
        if (!IsMapped()) {
            // Nothing to do — surface a clear error so the diagnostics thread
            // can flag this slot, but don't spam the apply count.
            const auto err = hf_valve_err_t::VALVE_ERR_DRIVER_NOT_AVAILABLE;
            SetState(ValveState::Uninitialized);
            RecordApplyResult(err, false);
            return err;
        }

        // First-pass configuration: PWM frequency + dither.
        if (!configured_.load(std::memory_order_acquire)) {
            const auto pwm_err = mgr.ConfigureProportionalPwm(
                entry_.manager_channel,
                static_cast<float>(entry_.model.pwm_hz));
            if (pwm_err != hf_valve_err_t::VALVE_SUCCESS) {
                RecordApplyResult(pwm_err, false);
                return pwm_err;
            }

            if (entry_.model.supports_dither &&
                entry_.model.dither_amplitude_ma > 0 &&
                entry_.model.dither_frequency_hz > 0) {
                const auto d_err = mgr.ConfigureProportionalDither(
                    entry_.manager_channel,
                    static_cast<float>(entry_.model.dither_amplitude_ma),
                    static_cast<float>(entry_.model.dither_frequency_hz));
                if (d_err != hf_valve_err_t::VALVE_SUCCESS) {
                    // Dither is non-fatal: log via last_error_ but keep going.
                    RecordApplyResult(d_err, false);
                }
            }

            configured_.store(true, std::memory_order_release);
        }

        const float pos = GetTargetPosition();
        const uint16_t cur_ma = PositionToCurrentMa(pos);

        // Drive the channel: enable on first non-zero command, disable on 0.
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
    // Diagnostics: TLE92466ED per-channel fault aggregation
    //----------------------------------------------------------------------

    hf_valve_err_t OnDiagnostics(ValveManager& mgr) noexcept override {
        if (!IsMapped()) {
            SetState(ValveState::Uninitialized);
            return hf_valve_err_t::VALVE_ERR_DRIVER_NOT_AVAILABLE;
        }

        tle92466ed::FaultReport report{};
        const hf_valve_err_t err = mgr.GetProportionalFaults(report);
        if (err != hf_valve_err_t::VALVE_SUCCESS) {
            last_diag_err_ = err;
            return err;
        }
        last_diag_err_ = hf_valve_err_t::VALVE_SUCCESS;

        const auto hw = static_cast<uint8_t>(entry_.manager_channel) -
                        static_cast<uint8_t>(ValveChannel::PROP_CH0);
        if (hw >= sizeof(report.channels) / sizeof(report.channels[0])) {
            return hf_valve_err_t::VALVE_ERR_INVALID_CHANNEL;
        }

        const auto& ch = report.channels[hw];
        if (ch.has_fault) {
            const hf_valve_err_t agg = hf_valve_err_t::VALVE_ERR_HARDWARE_FAULT;
            RecordFault(agg, now_ms_);
            return agg;
        }
        return hf_valve_err_t::VALVE_SUCCESS;
    }

    void SetDiagnosticTimestamp(uint64_t now_ms) noexcept { now_ms_ = now_ms; }
    hf_valve_err_t GetLastDiagError() const noexcept { return last_diag_err_; }

private:
    const ProportionalMapEntry& entry_;
    std::atomic<bool>           configured_{false};
    hf_valve_err_t              last_diag_err_{hf_valve_err_t::VALVE_SUCCESS};
    uint64_t                    now_ms_{0};
};

}  // namespace whvalve
