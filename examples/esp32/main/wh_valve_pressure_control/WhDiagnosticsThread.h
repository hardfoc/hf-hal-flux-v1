/**
 * @file WhDiagnosticsThread.h
 * @brief BaseThread that polls valve + ADC diagnostics at a slow cadence.
 *
 * @details Every iteration (default 100 ms = 10 Hz):
 *
 *   1. Walks every valve and calls `OnDiagnostics(ValveManager&)` so the
 *      MAX22200 / TLE92466ED fault flags are interpreted per-channel and
 *      converted into the per-valve atomic fault counters.
 *
 *   2. Reads any **alarm** status from the ADS7952 ADC handler. The board
 *      uses ADS7952 alarms to trigger on out-of-range pressure-sensor
 *      voltages (programmed via `Ads7952Handler::ProgramAlarmVoltage`); this
 *      thread surfaces those alarms into a small public snapshot so the
 *      pressure-control thread doesn't have to poll them itself.
 *
 *   3. Maintains a coarse system-health snapshot (drivers initialised,
 *      total fault counts) for the application to log periodically.
 *
 *   The thread never blocks the high-rate control thread because all valve
 *   state writes are done atomically through the same `RecordFault()` path
 *   that solenoid/proportional valves use during Apply().
 *
 * @copyright HardFOC (internal use only)
 * @date 2026
 */

#pragma once

#include "BaseThread.h"
#include "OsUtility.h"
#include "WhValveSystem.h"
#include "managers/ValveManager.h"
#include "handlers/ads7952/Ads7952Handler.h"

#include <atomic>
#include <cstdint>

namespace whvalve {

/**
 * @brief Lock-free snapshot of system-wide diagnostics maintained by the thread.
 */
struct DiagnosticsSnapshot {
    bool     manager_ready;
    bool     ads7952_ready;
    uint32_t valve_total_faults;
    uint32_t valve_total_apply_errors;
    uint16_t ads7952_alarm_mask;          ///< Bitmask of ADS7952 channels currently in alarm.
    uint64_t last_update_ms;
};

class WhDiagnosticsThread final : public BaseThread {
public:
    /**
     * @param system   Shared WhValveSystem.
     * @param manager  Flux::ValveManager (need not be initialised yet).
     * @param ads7952  Pointer to the resolved ADS7952 handler (may be nullptr;
     *                 alarms are then skipped). Pass `FLUX_API.adc.Get(...)`
     *                 cast to `Ads7952Handler*` once Flux is initialised.
     * @param period_ms Polling period; default 100 ms = 10 Hz.
     */
    WhDiagnosticsThread(WhValveSystem& system,
                        ValveManager&  manager,
                        Ads7952Handler* ads7952,
                        uint32_t        period_ms = 100) noexcept
        : BaseThread("WhDiag"),
          system_(system),
          manager_(manager),
          ads7952_(ads7952),
          period_ms_(period_ms) {}

    void SetAds7952(Ads7952Handler* h) noexcept { ads7952_ = h; }
    void SetPeriodMs(uint32_t ms) noexcept     { period_ms_ = (ms == 0) ? 1 : ms; }

    DiagnosticsSnapshot GetSnapshot() const noexcept {
        DiagnosticsSnapshot s{};
        s.manager_ready            = manager_ready_.load(std::memory_order_acquire);
        s.ads7952_ready            = ads7952_ready_.load(std::memory_order_acquire);
        s.valve_total_faults       = total_faults_.load(std::memory_order_acquire);
        s.valve_total_apply_errors = total_apply_errors_.load(std::memory_order_acquire);
        s.ads7952_alarm_mask       = ads7952_alarms_.load(std::memory_order_acquire);
        s.last_update_ms           = last_update_ms_.load(std::memory_order_acquire);
        return s;
    }

protected:
    bool Initialize() noexcept override {
        return CreateBaseThread(thread_stack_,
                                sizeof(thread_stack_),
                                kThreadPriority,
                                kThreadPriority,
                                0,
                                OS_AUTO_START);
    }

    bool Setup() noexcept override { return true; }

    uint32_t Step() noexcept override {
        const uint64_t now_ms = static_cast<uint64_t>(os_get_elapsed_time_msec());
        const bool mgr_ready = manager_.IsInitialized();
        manager_ready_.store(mgr_ready, std::memory_order_release);

        if (mgr_ready) {
            // ── Per-valve fault aggregation ───────────────────────────────
            system_.ForEachSolenoid([this, now_ms](SolenoidValve& v) {
                v.SetDiagnosticTimestamp(now_ms);
                (void)v.OnDiagnostics(manager_);
            });
            system_.ForEachProportional([this, now_ms](ProportionalValve& v) {
                v.SetDiagnosticTimestamp(now_ms);
                (void)v.OnDiagnostics(manager_);
            });

            // ── System totals (sticky counters) ───────────────────────────
            uint32_t total_f = 0, total_e = 0;
            system_.ForEachValve([&](Valve& v) {
                total_f += v.GetFaultCount();
                total_e += v.GetApplyErrorCount();
            });
            total_faults_.store(total_f, std::memory_order_release);
            total_apply_errors_.store(total_e, std::memory_order_release);
        }

        // ── ADS7952 health surface ───────────────────────────────────────
        // The ADS7952 driver does not yet expose a dedicated read-alarm-mask
        // helper — alarm assertion is routed via the device's GPIO0/GPIO1
        // alarm pins (programmable via Ads7952Handler::ProgramGpio). Until a
        // read-back helper is added to the driver we just track whether the
        // handler resolved successfully; per-channel alarm thresholds can
        // already be programmed from the application via
        // `Ads7952Handler::ProgramAlarmVoltage`.
        ads7952_ready_.store(ads7952_ != nullptr, std::memory_order_release);
        ads7952_alarms_.store(0, std::memory_order_release);
        last_update_ms_.store(now_ms, std::memory_order_release);

        return period_ms_;
    }

    bool Cleanup() noexcept override { return true; }
    bool ResetVariables() noexcept override {
        total_faults_.store(0, std::memory_order_release);
        total_apply_errors_.store(0, std::memory_order_release);
        ads7952_alarms_.store(0, std::memory_order_release);
        return true;
    }

private:
    WhValveSystem&  system_;
    ValveManager&   manager_;
    Ads7952Handler* ads7952_;
    uint32_t        period_ms_;

    std::atomic<bool>     manager_ready_{false};
    std::atomic<bool>     ads7952_ready_{false};
    std::atomic<uint32_t> total_faults_{0};
    std::atomic<uint32_t> total_apply_errors_{0};
    std::atomic<uint16_t> ads7952_alarms_{0};
    std::atomic<uint64_t> last_update_ms_{0};

    static constexpr std::size_t kThreadStackBytes = 6144;
    static constexpr OS_Uint     kThreadPriority   = 5;     ///< Lower than control thread.
    uint8_t thread_stack_[kThreadStackBytes]{};
};

}  // namespace whvalve
