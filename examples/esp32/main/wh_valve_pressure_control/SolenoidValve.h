/**
 * @file SolenoidValve.h
 * @brief Concrete valve type for Parker C15/C21 cartridge solenoids.
 *
 * @details Wraps a Flux::ValveManager solenoid channel (MAX22200 backend) with
 *          the C15/C21 hit-and-hold profile pulled from `WhValveCatalog.h`.
 *
 *          Position semantics: 0 → de-energised, > 0 → energised. The class
 *          maintains a one-shot "configured" flag — the CDR profile is pushed
 *          on the very first apply and again whenever a fault re-init is
 *          required (call `ForceReconfigure()`).
 *
 * @copyright HardFOC (internal use only)
 * @date 2026
 */

#pragma once

#include "Valve.h"
#include "WhValveCatalog.h"
#include "managers/ValveManager.h"

namespace whvalve {

class SolenoidValve final : public Valve {
public:
    SolenoidValve(const SolenoidMapEntry& entry) noexcept
        : Valve(entry.label), entry_(entry) {}

    ValveChannel ManagerChannel() const noexcept override { return entry_.manager_channel; }

    /// Force a re-configuration of the CDR profile on the next Apply().
    void ForceReconfigure() noexcept { configured_.store(false, std::memory_order_release); }

    const SolenoidModelSpec& Model() const noexcept { return entry_.model; }

    //----------------------------------------------------------------------
    // Manager-internal API (control thread)
    //----------------------------------------------------------------------

    hf_valve_err_t Apply(ValveManager& mgr) noexcept override {
        // Lazily push the C15/C21 CDR profile the first time we run, or after
        // a fault sequence cleared the configured flag.
        if (!configured_.load(std::memory_order_acquire)) {
            const auto err = mgr.ConfigureSolenoidCdr(entry_.manager_channel,
                                                     entry_.model.hit_current_ma,
                                                     entry_.model.hold_current_ma,
                                                     entry_.model.min_hit_time_ms);
            if (err != hf_valve_err_t::VALVE_SUCCESS) {
                RecordApplyResult(err, false);
                return err;
            }
            configured_.store(true, std::memory_order_release);
        }

        // Boolean target: any positive request energises.
        const float target = GetTargetPosition();
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

    //----------------------------------------------------------------------
    // Diagnostics: MAX22200 fault-bit interpretation per channel
    //----------------------------------------------------------------------

    hf_valve_err_t OnDiagnostics(ValveManager& mgr) noexcept override {
        max22200::FaultStatus faults{};
        const hf_valve_err_t err = mgr.GetSolenoidFaults(faults);
        if (err != hf_valve_err_t::VALVE_SUCCESS) {
            // Driver not initialised or SPI bus issue: surface the error
            // without spamming per-channel fault counts.
            last_diag_err_ = err;
            return err;
        }
        last_diag_err_ = hf_valve_err_t::VALVE_SUCCESS;

        // MAX22200 exposes per-channel fault bitmasks: OCP (overcurrent),
        // HHF (hit not reached), OLF (open load), DPM (plunger movement).
        // Any bit set on this channel triggers a per-valve fault.
        const auto hw = static_cast<uint8_t>(entry_.manager_channel) -
                        static_cast<uint8_t>(ValveChannel::SOL_CH0);
        const uint8_t ch_bit = static_cast<uint8_t>(1U << hw);

        const bool ocp = (faults.overcurrent_channel_mask           & ch_bit) != 0;
        const bool hhf = (faults.hit_not_reached_channel_mask       & ch_bit) != 0;
        const bool olf = (faults.open_load_fault_channel_mask       & ch_bit) != 0;
        const bool dpm = (faults.plunger_movement_fault_channel_mask & ch_bit) != 0;

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
    const SolenoidMapEntry&  entry_;
    std::atomic<bool>        configured_{false};
    hf_valve_err_t           last_diag_err_{hf_valve_err_t::VALVE_SUCCESS};
    uint64_t                 now_ms_{0};
};

}  // namespace whvalve
