/**
 * @file WhDiagnosticsThread.h
 * @brief BaseThread that polls valve + driver-IC + ADC diagnostics.
 *
 * @details Every iteration (default 100 ms = 10 Hz):
 *
 *   1. Walks every valve and calls `OnDiagnostics(ValveManager&)` so each
 *      MAX22200 / TLE92466ED per-channel fault flag is decoded and the
 *      per-valve diagnostic snapshot is refreshed.
 *
 *   2. Reads the device-level (global) fault registers from both driver ICs
 *      and aggregates them into `SubsystemFaultSnapshot`. This is the
 *      single source of truth the application can poll for "is anything
 *      bad happening at the chip level?":
 *        - MAX22200: OVT (die over-temperature), COMER (SPI comm error),
 *          UVM (under-voltage), per-fault masks.
 *        - TLE92466ED: VBAT/VIO/VDD UV/OV, central OT error/warning,
 *          clock fault, SPI watchdog, ECC errors, mission-mode flag.
 *
 *   3. Tracks ADS7952 handler readiness — alarm-bit scrape is left as a
 *      TODO until the ADS7952 driver exposes an alarm-readback helper.
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
#include "handlers/max22200/Max22200Handler.h"
#include "handlers/tle92466ed/Tle92466edHandler.h"
#include "core/hf-core-drivers/external/hf-max22200-driver/inc/max22200_types.hpp"
#include "core/hf-core-drivers/external/hf-tle92466ed-driver/inc/tle92466ed.hpp"

#include <atomic>
#include <cstdint>

namespace whvalve {

/**
 * @brief Lock-free snapshot of subsystem-wide health, refreshed by the thread.
 *
 * All flags reflect the most recent successful read of the corresponding
 * driver-IC global register. `manager_ready` / `*_ready` mean the handler is
 * accessible — when false the rest of the snapshot is stale.
 */
struct SubsystemFaultSnapshot {
    bool     manager_ready{false};
    bool     max22200_ready{false};
    bool     tle92466_ready{false};
    bool     ads7952_ready{false};

    // ── MAX22200 global flags (STATUS register) ──────────────────────────
    bool     max_overtemperature{false};
    bool     max_communication_error{false};
    bool     max_undervoltage{false};
    bool     max_global_overcurrent{false};
    bool     max_global_open_load{false};
    bool     max_global_hit_not_reached{false};
    bool     max_global_plunger_movement{false};
    bool     max_active{false};            ///< Global ACTIVE bit (driver enabled).
    uint8_t  max_channels_on_mask{0};

    // ── TLE92466ED global flags (FaultReport) ────────────────────────────
    bool     tle_vbat_uv{false};
    bool     tle_vbat_ov{false};
    bool     tle_vio_uv{false};
    bool     tle_vio_ov{false};
    bool     tle_vdd_uv{false};
    bool     tle_vdd_ov{false};
    bool     tle_ot_warning{false};
    bool     tle_ot_error{false};
    bool     tle_clock_fault{false};
    bool     tle_spi_wd_error{false};
    bool     tle_por_event{false};
    bool     tle_reset_event{false};
    bool     tle_reg_ecc_err{false};
    bool     tle_otp_ecc_err{false};
    bool     tle_supply_nok_internal{false};
    bool     tle_supply_nok_external{false};
    uint16_t tle_vbat_voltage_raw{0};
    uint16_t tle_vio_voltage_raw{0};

    // ── Aggregates ───────────────────────────────────────────────────────
    uint32_t valve_total_faults{0};
    uint32_t valve_total_apply_errors{0};
    uint64_t last_update_ms{0};
};

class WhDiagnosticsThread final : public BaseThread {
public:
    /**
     * @param system   Shared WhValveSystem.
     * @param manager  Flux::ValveManager (need not be initialised yet).
     * @param ads7952  Pointer to the resolved ADS7952 handler (may be nullptr;
     *                 readiness flag will then read false).
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

    /// Lock-free read of the latest subsystem-wide health snapshot.
    SubsystemFaultSnapshot GetSnapshot() const noexcept {
        SubsystemFaultSnapshot s = snapshot_template_;
        const uint32_t f1 = max_flags_.load(std::memory_order_acquire);
        s.manager_ready             = manager_ready_.load(std::memory_order_acquire);
        s.max22200_ready            = max22200_ready_.load(std::memory_order_acquire);
        s.tle92466_ready            = tle92466_ready_.load(std::memory_order_acquire);
        s.ads7952_ready             = ads7952_ready_.load(std::memory_order_acquire);
        s.max_overtemperature       = (f1 & kMaxOvt)  != 0;
        s.max_communication_error   = (f1 & kMaxComer)!= 0;
        s.max_undervoltage          = (f1 & kMaxUvm)  != 0;
        s.max_global_overcurrent    = (f1 & kMaxOcp)  != 0;
        s.max_global_open_load      = (f1 & kMaxOlf)  != 0;
        s.max_global_hit_not_reached= (f1 & kMaxHhf)  != 0;
        s.max_global_plunger_movement = (f1 & kMaxDpm)!= 0;
        s.max_active                = (f1 & kMaxActive) != 0;
        s.max_channels_on_mask      = max_channels_on_mask_.load(std::memory_order_acquire);

        const uint32_t f2 = tle_flags_.load(std::memory_order_acquire);
        s.tle_vbat_uv          = (f2 & kTleVbatUv) != 0;
        s.tle_vbat_ov          = (f2 & kTleVbatOv) != 0;
        s.tle_vio_uv           = (f2 & kTleVioUv)  != 0;
        s.tle_vio_ov           = (f2 & kTleVioOv)  != 0;
        s.tle_vdd_uv           = (f2 & kTleVddUv)  != 0;
        s.tle_vdd_ov           = (f2 & kTleVddOv)  != 0;
        s.tle_ot_warning       = (f2 & kTleOtWarn) != 0;
        s.tle_ot_error         = (f2 & kTleOtErr)  != 0;
        s.tle_clock_fault      = (f2 & kTleClk)    != 0;
        s.tle_spi_wd_error     = (f2 & kTleSpiWd)  != 0;
        s.tle_por_event        = (f2 & kTlePor)    != 0;
        s.tle_reset_event      = (f2 & kTleReset)  != 0;
        s.tle_reg_ecc_err      = (f2 & kTleRegEcc) != 0;
        s.tle_otp_ecc_err      = (f2 & kTleOtpEcc) != 0;
        s.tle_supply_nok_internal = (f2 & kTleSupNokInt) != 0;
        s.tle_supply_nok_external = (f2 & kTleSupNokExt) != 0;
        s.tle_vbat_voltage_raw = tle_vbat_raw_.load(std::memory_order_acquire);
        s.tle_vio_voltage_raw  = tle_vio_raw_.load(std::memory_order_acquire);

        s.valve_total_faults       = total_faults_.load(std::memory_order_acquire);
        s.valve_total_apply_errors = total_apply_errors_.load(std::memory_order_acquire);
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

            // ── MAX22200 global STATUS scrape ─────────────────────────────
            ScrapeMax22200();
            // ── TLE92466ED global FaultReport scrape ──────────────────────
            ScrapeTle92466();
        } else {
            max22200_ready_.store(false, std::memory_order_release);
            tle92466_ready_.store(false, std::memory_order_release);
            max_flags_.store(0, std::memory_order_release);
            tle_flags_.store(0, std::memory_order_release);
        }

        ads7952_ready_.store(ads7952_ != nullptr, std::memory_order_release);
        last_update_ms_.store(now_ms, std::memory_order_release);
        return period_ms_;
    }

    bool Cleanup() noexcept override { return true; }
    bool ResetVariables() noexcept override {
        total_faults_.store(0, std::memory_order_release);
        total_apply_errors_.store(0, std::memory_order_release);
        max_flags_.store(0, std::memory_order_release);
        tle_flags_.store(0, std::memory_order_release);
        return true;
    }

private:
    // Bitfield encodings for MAX22200 / TLE92466ED global flag aggregation.
    static constexpr uint32_t kMaxOvt        = 1U << 0;
    static constexpr uint32_t kMaxComer      = 1U << 1;
    static constexpr uint32_t kMaxUvm        = 1U << 2;
    static constexpr uint32_t kMaxOcp        = 1U << 3;
    static constexpr uint32_t kMaxOlf        = 1U << 4;
    static constexpr uint32_t kMaxHhf        = 1U << 5;
    static constexpr uint32_t kMaxDpm        = 1U << 6;
    static constexpr uint32_t kMaxActive     = 1U << 7;

    static constexpr uint32_t kTleVbatUv     = 1U << 0;
    static constexpr uint32_t kTleVbatOv     = 1U << 1;
    static constexpr uint32_t kTleVioUv      = 1U << 2;
    static constexpr uint32_t kTleVioOv      = 1U << 3;
    static constexpr uint32_t kTleVddUv      = 1U << 4;
    static constexpr uint32_t kTleVddOv      = 1U << 5;
    static constexpr uint32_t kTleOtWarn     = 1U << 6;
    static constexpr uint32_t kTleOtErr      = 1U << 7;
    static constexpr uint32_t kTleClk        = 1U << 8;
    static constexpr uint32_t kTleSpiWd      = 1U << 9;
    static constexpr uint32_t kTlePor        = 1U << 10;
    static constexpr uint32_t kTleReset      = 1U << 11;
    static constexpr uint32_t kTleRegEcc     = 1U << 12;
    static constexpr uint32_t kTleOtpEcc     = 1U << 13;
    static constexpr uint32_t kTleSupNokInt  = 1U << 14;
    static constexpr uint32_t kTleSupNokExt  = 1U << 15;

    void ScrapeMax22200() noexcept {
        Max22200Handler* h = manager_.GetMax22200Handler();
        if (!h) {
            max22200_ready_.store(false, std::memory_order_release);
            max_flags_.store(0, std::memory_order_release);
            return;
        }
        max22200::StatusConfig st{};
        const auto rs = h->GetStatus(st);
        if (rs != max22200::DriverStatus::OK) {
            max22200_ready_.store(false, std::memory_order_release);
            return;
        }
        max22200_ready_.store(true, std::memory_order_release);
        uint32_t bits = 0;
        if (st.overtemperature)        bits |= kMaxOvt;
        if (st.communication_error)    bits |= kMaxComer;
        if (st.undervoltage)           bits |= kMaxUvm;
        if (st.overcurrent)            bits |= kMaxOcp;
        if (st.open_load_fault)        bits |= kMaxOlf;
        if (st.hit_not_reached)        bits |= kMaxHhf;
        if (st.plunger_movement_fault) bits |= kMaxDpm;
        if (st.active)                 bits |= kMaxActive;
        max_flags_.store(bits, std::memory_order_release);
        max_channels_on_mask_.store(st.channels_on_mask, std::memory_order_release);
    }

    void ScrapeTle92466() noexcept {
        tle92466ed::FaultReport report{};
        const hf_valve_err_t err = manager_.GetProportionalFaults(report);
        if (err != hf_valve_err_t::VALVE_SUCCESS) {
            tle92466_ready_.store(false, std::memory_order_release);
            return;
        }
        tle92466_ready_.store(true, std::memory_order_release);
        uint32_t bits = 0;
        if (report.vbat_uv)         bits |= kTleVbatUv;
        if (report.vbat_ov)         bits |= kTleVbatOv;
        if (report.vio_uv)          bits |= kTleVioUv;
        if (report.vio_ov)          bits |= kTleVioOv;
        if (report.vdd_uv)          bits |= kTleVddUv;
        if (report.vdd_ov)          bits |= kTleVddOv;
        if (report.ot_warning)      bits |= kTleOtWarn;
        if (report.ot_error)        bits |= kTleOtErr;
        if (report.clock_fault)     bits |= kTleClk;
        if (report.spi_wd_error)    bits |= kTleSpiWd;
        if (report.por_event)       bits |= kTlePor;
        if (report.reset_event)     bits |= kTleReset;
        if (report.reg_ecc_err)     bits |= kTleRegEcc;
        if (report.otp_ecc_err)     bits |= kTleOtpEcc;
        if (report.supply_nok_internal) bits |= kTleSupNokInt;
        if (report.supply_nok_external) bits |= kTleSupNokExt;
        tle_flags_.store(bits, std::memory_order_release);
        // Optionally also pull live VBAT/VIO from DeviceStatus (separate
        // call) — the FaultReport does not include voltages.
        Tle92466edHandler* h = manager_.GetTle92466Handler();
        if (h) {
            tle92466ed::DeviceStatus dev{};
            // GetDeviceStatus exists on the underlying driver but the
            // handler does not currently expose it. Fall through silently
            // — the per-channel ChannelDiagnostics already publish
            // average_current / vbat_feedback on every proportional valve.
            (void)dev;
        }
    }

    // Templated empty snapshot used to seed GetSnapshot() reads.
    static const SubsystemFaultSnapshot snapshot_template_;

    WhValveSystem&  system_;
    ValveManager&   manager_;
    Ads7952Handler* ads7952_;
    uint32_t        period_ms_;

    std::atomic<bool>     manager_ready_{false};
    std::atomic<bool>     max22200_ready_{false};
    std::atomic<bool>     tle92466_ready_{false};
    std::atomic<bool>     ads7952_ready_{false};
    std::atomic<uint32_t> total_faults_{0};
    std::atomic<uint32_t> total_apply_errors_{0};
    std::atomic<uint32_t> max_flags_{0};
    std::atomic<uint32_t> tle_flags_{0};
    std::atomic<uint8_t>  max_channels_on_mask_{0};
    std::atomic<uint16_t> tle_vbat_raw_{0};
    std::atomic<uint16_t> tle_vio_raw_{0};
    std::atomic<uint64_t> last_update_ms_{0};

    static constexpr std::size_t kThreadStackBytes = 6144;
    static constexpr OS_Uint     kThreadPriority   = 5;     ///< Lower than control thread.
    uint8_t thread_stack_[kThreadStackBytes]{};
};

inline const SubsystemFaultSnapshot WhDiagnosticsThread::snapshot_template_{};

}  // namespace whvalve
