/**
 * @file WhValveSelfTest.h
 * @brief Boot-time self-test that exercises every WH bench valve in sequence
 *        and surfaces any per-valve diagnostic the driver IC reports.
 *
 * @details Mirrors the per-driver test patterns we built up while bringing
 *          up the MAX22200 and TLE92466ED on the Flux board:
 *
 *            - Solenoids (MAX22200): toggle ON/OFF a few times with settling
 *              + diagnostic-poll windows in between, exactly like the
 *              `c21_cycle_test` in the MAX22200 driver examples folder.
 *
 *            - Proportionals (TLE92466ED): ramp from 0 to full flow and back
 *              while sampling the live `FB_I_AVG` telemetry, exactly like
 *              the `lm_pro_cycle_test` in the TLE92466ED examples folder.
 *
 *          The self-test does NOT bypass the existing `WhValveControlThread`
 *          / `WhDiagnosticsThread`; it is layered on top of them. It sets
 *          the per-valve target via `RequestPosition` (the same lock-free
 *          API any control loop would use) and waits for the control thread
 *          to push it. Diagnostics are read back through `Snapshot()` after
 *          the diagnostics thread has had at least one full pass.
 *
 *          The user can only physically connect ONE valve to the manifold
 *          at a time on this bench, so the self-test pauses between valves
 *          for `kInterValvePauseMs` to let the operator move the connection.
 *
 *          Per-valve enable masks let the operator skip valves that are not
 *          currently fitted — see `SelfTestPlan` below. The default is
 *          "test every solenoid then every proportional".
 *
 * @copyright HardFOC (internal use only)
 * @date 2026
 */

#pragma once

#include "OsUtility.h"
#include "WhDiagnosticsThread.h"
#include "WhValveSystem.h"

#include "esp_log.h"

#include <array>
#include <cstdint>
#include <cstdio>

namespace whvalve {

/**
 * @brief Per-valve enable mask + tunable knobs for the boot self-test.
 *
 * Defaults: every valve in the catalog is tested; the user can flip any
 * `enable_*` flag to false in the application before calling
 * `WhValveSelfTest::Run`.
 */
struct SelfTestPlan {
    /// One bit per `SolenoidId`; index = static_cast<size_t>(id).
    std::array<bool, kNumSolenoids>     enable_solenoids{};
    /// One bit per `ProportionalId`; index = static_cast<size_t>(id).
    std::array<bool, kNumProportionals> enable_proportionals{};

    /// How long each ON pulse lasts during a solenoid cycle (ms).
    uint32_t solenoid_on_ms{500};
    /// How long the OFF dwell lasts between solenoid pulses (ms).
    uint32_t solenoid_off_ms{300};
    /// How many ON/OFF cycles per solenoid.
    uint32_t solenoid_cycles{2};

    /// Number of intermediate setpoints captured during the 0 → full → 0
    /// triangle ramp (must be ≥ 4). Total visited setpoints = 2 × steps + 1.
    uint32_t proportional_steps{8};
    /// Settle time at each step before sampling current (ms). Must be ≥
    /// (1 / dither_freq + 1 diagnostic period). 80 ms covers a 50 Hz dither
    /// (20 ms) plus two 10 Hz diag ticks.
    uint32_t proportional_settle_ms{80};
    /// Tolerance band for the per-step setpoint-vs-actual current check.
    /// `tracking_band_pct` of full-flow OR `tracking_band_min_mA` mA,
    /// whichever is greater. ±2 mA / ±5 % matches the LM-Pro
    /// characterization done in `lm_pro_cycle_test.cpp`.
    float    tracking_band_pct{5.0f};
    float    tracking_band_min_mA{2.0f};
    /// Maximum number of setpoints in the full triangle that may fall
    /// outside the band before the valve is marked FAIL. Set to ≥0; the
    /// LM-Pro typically passes everything ≥ ~10 mA so 2 generous misses are
    /// allowed for the noise floor at the very low end.
    uint32_t tracking_max_outliers{2};

    /// Pause between valves so the operator can move the manifold connection.
    uint32_t inter_valve_pause_ms{1500};

    /// If true, the self-test prompts before each valve and waits this long.
    /// Set to 0 to skip the prompt and run straight through.
    uint32_t prompt_wait_ms{1000};

    /// Default: enable everything.
    static constexpr SelfTestPlan All() noexcept {
        SelfTestPlan p{};
        for (auto& b : p.enable_solenoids)     b = true;
        for (auto& b : p.enable_proportionals) b = true;
        return p;
    }
};

/**
 * @brief Result of one valve self-test.
 *
 * `pass` is true when no fault flags were observed throughout the cycle and
 * the apply-side error counter never incremented.
 */
struct SelfTestResult {
    std::string_view label;
    ValveChannel     channel{};
    bool             pass{false};
    uint32_t         apply_errors{0};
    uint32_t         fault_count{0};

    // Proportional-only tracking metrics. Zero for solenoids.
    uint32_t tracking_samples{0};         ///< How many setpoints were measured.
    uint32_t tracking_outliers{0};        ///< How many fell outside the ±band.
    int16_t  tracking_max_abs_err_mA{0};  ///< Worst |setpoint − actual| in mA.
    int16_t  tracking_mean_err_mA{0};     ///< Mean (signed) error across the ramp.
    bool     ramp_monotonic_up{true};     ///< True if up-ramp actuals strictly ↑.
    bool     ramp_monotonic_down{true};   ///< True if down-ramp actuals strictly ↓.

    /// Aggregated, human-readable description of every fault flag seen.
    /// Empty when `pass == true`. Kept small to fit in the main-task stack.
    std::array<char, 128> fault_summary{};
};

/// Aggregated self-test report.
struct SelfTestReport {
    std::array<SelfTestResult, kNumSolenoids>     solenoids{};
    std::array<SelfTestResult, kNumProportionals> proportionals{};
    bool overall_pass{true};
};

/**
 * @brief Driver for the boot self-test.
 *
 * Owns no state; everything lives on the stack of `Run()`. The class exists
 * purely as a namespace for the helpers. Logs use the `WhSelfTest` ESP-IDF
 * tag at INFO level for normal flow and WARN/ERROR for faults.
 */
class WhValveSelfTest {
public:
    static constexpr const char* kTag = "WhSelfTest";

    /**
     * @brief Run the configured self-test plan synchronously on the caller.
     *
     * Caller MUST have brought up `WhValveControlThread` and
     * `WhDiagnosticsThread` already (otherwise targets won't reach hardware
     * and diagnostics won't be refreshed).
     *
     * @param system  Shared WhValveSystem instance.
     * @param diag    Diagnostics thread (used to pull subsystem-level health).
     * @param plan    Test plan (defaults to `SelfTestPlan::All()`).
     * @return Per-valve report. `overall_pass` is true if every enabled
     *         valve passed.
     */
    /**
     * @return Reference to a static report. Held by the self-test instance;
     *         do not call `Run()` concurrently from multiple threads.
     */
    static const SelfTestReport& Run(WhValveSystem&         system,
                                     WhDiagnosticsThread&   diag,
                                     const SelfTestPlan&    plan = SelfTestPlan::All()) noexcept {
        // Keep the (~3 kB) report off the main task's small stack.
        static SelfTestReport report{};
        report = SelfTestReport{};

        ESP_LOGI(kTag, "");
        ESP_LOGI(kTag, "════════════════════════════════════════════════════════════");
        ESP_LOGI(kTag, "  WH Valve Self-Test — bench commissioning sweep");
        ESP_LOGI(kTag, "  one valve at a time, 1× ON/OFF or ramp + diagnostics");
        ESP_LOGI(kTag, "════════════════════════════════════════════════════════════");
        ESP_LOGI(kTag, "");

        // Make sure the diagnostics thread has at least one fresh pass before
        // we start so subsequent fault-count deltas are meaningful.
        WaitForDiagPass(diag);

        // ── Solenoid sweep ───────────────────────────────────────────────
        for (std::size_t i = 0; i < kNumSolenoids; ++i) {
            const auto& entry = kSolenoidMap[i];
            auto&       valve = system.Get(static_cast<SolenoidId>(i));

            if (!plan.enable_solenoids[i]) {
                ESP_LOGW(kTag, "[SOL %.*s] skipped (disabled in plan)",
                         static_cast<int>(entry.label.size()), entry.label.data());
                report.solenoids[i].label   = entry.label;
                report.solenoids[i].channel = entry.manager_channel;
                report.solenoids[i].pass    = true;  // skipped == not failed
                continue;
            }

            PromptOperator(plan, "SOL", entry.label, entry.model.label);
            report.solenoids[i] = TestSolenoid(valve, entry, plan, diag);
            if (!report.solenoids[i].pass) report.overall_pass = false;
            DwellBetweenValves(plan);
        }

        // ── Proportional sweep ──────────────────────────────────────────
        for (std::size_t i = 0; i < kNumProportionals; ++i) {
            const auto& entry = kProportionalMap[i];
            auto&       valve = system.Get(static_cast<ProportionalId>(i));

            if (!plan.enable_proportionals[i]) {
                ESP_LOGW(kTag, "[PROP %.*s] skipped (disabled in plan)",
                         static_cast<int>(entry.label.size()), entry.label.data());
                report.proportionals[i].label   = entry.label;
                report.proportionals[i].channel = entry.manager_channel;
                report.proportionals[i].pass    = true;
                continue;
            }

            PromptOperator(plan, "PROP", entry.label, entry.model.label);
            report.proportionals[i] = TestProportional(valve, entry, plan, diag);
            if (!report.proportionals[i].pass) report.overall_pass = false;
            DwellBetweenValves(plan);
        }

        PrintSummary(report);
        return report;
    }

private:
    //---------------------------------------------------------------------
    // Solenoid: ON/OFF cycle test
    //---------------------------------------------------------------------
    static SelfTestResult TestSolenoid(SolenoidValve&          valve,
                                       const SolenoidMapEntry& entry,
                                       const SelfTestPlan&     plan,
                                       WhDiagnosticsThread&    diag) noexcept {
        SelfTestResult res{};
        res.label   = entry.label;
        res.channel = entry.manager_channel;

        const uint32_t apply_errors_before = valve.GetApplyErrorCount();
        const uint32_t fault_count_before  = valve.GetFaultCount();

        ESP_LOGI(kTag,
                 "[SOL %.*s] CH=SOL_CH%u  model=%.*s  cycles=%lu  on=%lu ms  off=%lu ms",
                 static_cast<int>(entry.label.size()), entry.label.data(),
                 ChannelHwIndex(entry.manager_channel),
                 static_cast<int>(entry.model.label.size()),
                 entry.model.label.data(),
                 static_cast<unsigned long>(plan.solenoid_cycles),
                 static_cast<unsigned long>(plan.solenoid_on_ms),
                 static_cast<unsigned long>(plan.solenoid_off_ms));

        // Make sure the valve starts from de-energised.
        valve.RequestState(false);
        os_delay_msec(200);
        WaitForDiagPass(diag);

        for (uint32_t cycle = 0; cycle < plan.solenoid_cycles; ++cycle) {
            valve.RequestState(true);
            ESP_LOGI(kTag, "  cycle %lu/%lu — ON  (target=1.0)",
                     static_cast<unsigned long>(cycle + 1),
                     static_cast<unsigned long>(plan.solenoid_cycles));
            os_delay_msec(plan.solenoid_on_ms);
            WaitForDiagPass(diag);
            LogSolenoidSnapshot(entry.label, valve);

            valve.RequestState(false);
            ESP_LOGI(kTag, "  cycle %lu/%lu — OFF (target=0.0)",
                     static_cast<unsigned long>(cycle + 1),
                     static_cast<unsigned long>(plan.solenoid_cycles));
            os_delay_msec(plan.solenoid_off_ms);
            WaitForDiagPass(diag);
        }

        // Final state: leave de-energised.
        valve.RequestState(false);
        os_delay_msec(200);

        res.apply_errors = valve.GetApplyErrorCount() - apply_errors_before;
        res.fault_count  = valve.GetFaultCount()      - fault_count_before;

        const auto snap = valve.Snapshot();
        FormatSolenoidFaults(snap, res.apply_errors, res.fault_summary);
        res.pass = (res.apply_errors == 0) && (res.fault_count == 0) &&
                   !snap.any_fault();

        ESP_LOGI(kTag, "[SOL %.*s] result: %s  apply_errs=%lu  fault_count=%lu",
                 static_cast<int>(entry.label.size()), entry.label.data(),
                 res.pass ? "PASS" : "FAIL",
                 static_cast<unsigned long>(res.apply_errors),
                 static_cast<unsigned long>(res.fault_count));
        if (!res.pass) {
            ESP_LOGE(kTag, "[SOL %.*s] FAULTS: %s",
                     static_cast<int>(entry.label.size()), entry.label.data(),
                     res.fault_summary.data());
        }
        ESP_LOGI(kTag, "");
        return res;
    }

    //---------------------------------------------------------------------
    // Proportional: 0 → full → 0 triangle ramp + setpoint-vs-actual tracking
    //---------------------------------------------------------------------
    //
    // For each setpoint visited along the triangle we:
    //   1. Request the position (lock-free atomic, picked up by the
    //      WhValveControlThread on its next tick).
    //   2. Sleep `proportional_settle_ms` (covers ≥ one dither period + one
    //      diagnostic poll).
    //   3. Wait one full diagnostics-thread sweep so the snapshot's
    //      `average_current_mA` reflects the new setpoint.
    //   4. Compare measured |I_avg − setpoint| against
    //      `max(tracking_band_min_mA, tracking_band_pct% × full_flow)`.
    //
    // Up-ramp actuals must strictly increase and down-ramp actuals must
    // strictly decrease (with a small 1 mA hysteresis to absorb measurement
    // noise) — that's what catches "the valve doesn't actually track the
    // setpoint" failures (stuck plunger, broken coil, EN pin LOW, …).
    static SelfTestResult TestProportional(ProportionalValve&         valve,
                                           const ProportionalMapEntry& entry,
                                           const SelfTestPlan&         plan,
                                           WhDiagnosticsThread&        diag) noexcept {
        SelfTestResult res{};
        res.label   = entry.label;
        res.channel = entry.manager_channel;

        const uint32_t apply_errors_before = valve.GetApplyErrorCount();
        const uint32_t fault_count_before  = valve.GetFaultCount();

        const uint32_t steps = (plan.proportional_steps < 4) ? 4 : plan.proportional_steps;
        const uint16_t full_flow_ma = entry.model.full_flow_current_ma;
        const float band_pct  = (plan.tracking_band_pct * full_flow_ma) / 100.0f;
        const float band_mA   = (band_pct > plan.tracking_band_min_mA)
                                ? band_pct : plan.tracking_band_min_mA;

        ESP_LOGI(kTag,
                 "[PROP %.*s] CH=PROP_CH%u  model=%.*s  triangle 0→%u→0 mA  "
                 "steps=%lu  band=±%.1f mA",
                 static_cast<int>(entry.label.size()), entry.label.data(),
                 ChannelHwIndex(entry.manager_channel),
                 static_cast<int>(entry.model.label.size()),
                 entry.model.label.data(),
                 static_cast<unsigned>(full_flow_ma),
                 static_cast<unsigned long>(steps),
                 static_cast<double>(band_mA));

        // Reset to 0 first.
        valve.RequestPosition(0.0f);
        os_delay_msec(80);
        WaitForDiagPass(diag);

        int32_t  err_sum_mA  = 0;
        uint32_t err_count   = 0;
        int16_t  max_abs_err = 0;
        int16_t  prev_actual = 0;
        bool     first_sample = true;

        auto walk_and_check = [&](float pos, const char* tag, bool ascending) {
            valve.RequestPosition(pos);
            os_delay_msec(plan.proportional_settle_ms);
            WaitForDiagPass(diag);

            const auto snap = valve.Snapshot();
            const int16_t actual_mA = snap.average_current_mA;
            const int16_t cmd_mA = static_cast<int16_t>(
                pos * static_cast<float>(full_flow_ma) + 0.5f);
            const int16_t err_mA = static_cast<int16_t>(actual_mA - cmd_mA);
            const int16_t abs_err_mA = static_cast<int16_t>(err_mA < 0 ? -err_mA : err_mA);

            const bool inside_band =
                static_cast<float>(abs_err_mA) <= band_mA;
            if (!inside_band) ++res.tracking_outliers;
            err_sum_mA += err_mA;
            ++err_count;
            if (abs_err_mA > max_abs_err) max_abs_err = abs_err_mA;

            // Monotonicity check (1 mA hysteresis to absorb chip noise).
            if (!first_sample) {
                const int delta = actual_mA - prev_actual;
                if (ascending && delta < -1) res.ramp_monotonic_up   = false;
                if (!ascending && delta > 1) res.ramp_monotonic_down = false;
            }
            first_sample = false;
            prev_actual  = actual_mA;

            ESP_LOGI(kTag,
                     "    %.*s [%s]: pos=%.2f cmd=%4d mA  I_act=%5d mA  "
                     "err=%+4d mA (%s)  duty=%4u/10000  flags=[oc=%d sg=%d ol=%d ot=%d olsg=%d]"
                     "  warn=[ot=%d cur=%d pwm=%d olsg=%d]",
                     static_cast<int>(entry.label.size()), entry.label.data(),
                     tag,
                     static_cast<double>(pos),
                     static_cast<int>(cmd_mA),
                     static_cast<int>(actual_mA),
                     static_cast<int>(err_mA),
                     inside_band ? "OK" : "OUT",
                     snap.duty_cycle_permyriad,
                     snap.overcurrent, snap.short_to_ground, snap.open_load,
                     snap.over_temperature, snap.open_load_short_ground,
                     snap.ot_warning, snap.current_regulation_warning,
                     snap.pwm_regulation_warning, snap.olsg_warning);
        };

        // Ramp UP 0 → 1
        for (uint32_t s = 0; s <= steps; ++s) {
            const float pos = static_cast<float>(s) / static_cast<float>(steps);
            walk_and_check(pos, "UP  ", /*ascending=*/true);
        }
        // Ramp DOWN 1 → 0
        first_sample = true;  // restart monotonicity check for the down-ramp
        for (int32_t s = static_cast<int32_t>(steps) - 1; s >= 0; --s) {
            const float pos = static_cast<float>(s) / static_cast<float>(steps);
            walk_and_check(pos, "DOWN", /*ascending=*/false);
        }

        valve.RequestPosition(0.0f);
        os_delay_msec(80);

        res.apply_errors           = valve.GetApplyErrorCount() - apply_errors_before;
        res.fault_count            = valve.GetFaultCount()      - fault_count_before;
        res.tracking_samples       = err_count;
        res.tracking_max_abs_err_mA = max_abs_err;
        res.tracking_mean_err_mA   = (err_count > 0)
            ? static_cast<int16_t>(err_sum_mA / static_cast<int32_t>(err_count))
            : 0;

        const auto snap = valve.Snapshot();
        const bool tracking_ok = (res.tracking_outliers <= plan.tracking_max_outliers) &&
                                 res.ramp_monotonic_up && res.ramp_monotonic_down;
        res.pass = (res.apply_errors == 0) && (res.fault_count == 0) &&
                   !snap.any_fault() && tracking_ok;

        FormatProportionalFaults(snap, res.apply_errors, valve.GetLastError(),
                                 res, res.fault_summary);

        ESP_LOGI(kTag,
                 "[PROP %.*s] result: %s  apply_errs=%lu  fc=%lu  "
                 "tracking[outliers=%lu/%lu  max|err|=%d mA  mean=%+d mA  "
                 "↑mono=%d ↓mono=%d]",
                 static_cast<int>(entry.label.size()), entry.label.data(),
                 res.pass ? "PASS" : "FAIL",
                 static_cast<unsigned long>(res.apply_errors),
                 static_cast<unsigned long>(res.fault_count),
                 static_cast<unsigned long>(res.tracking_outliers),
                 static_cast<unsigned long>(res.tracking_samples),
                 static_cast<int>(res.tracking_max_abs_err_mA),
                 static_cast<int>(res.tracking_mean_err_mA),
                 res.ramp_monotonic_up, res.ramp_monotonic_down);
        if (!res.pass) {
            ESP_LOGE(kTag, "[PROP %.*s] FAULTS: %s",
                     static_cast<int>(entry.label.size()), entry.label.data(),
                     res.fault_summary.data());
        }
        ESP_LOGI(kTag, "");
        return res;
    }

    //---------------------------------------------------------------------
    // Helpers
    //---------------------------------------------------------------------

    /// Map a `ValveChannel` to its 0-based hardware index within the chip.
    static constexpr uint32_t ChannelHwIndex(ValveChannel ch) noexcept {
        const auto v = static_cast<uint32_t>(ch);
        const auto p = static_cast<uint32_t>(ValveChannel::PROP_CH0);
        if (v >= p) return v - p;            // PROP_CHn
        return v - static_cast<uint32_t>(ValveChannel::SOL_CH0);  // SOL_CHn
    }

    /// Wait until the diagnostics thread has completed one fresh full pass
    /// after the call returns. Without this we'd race the snapshot read.
    static void WaitForDiagPass(WhDiagnosticsThread& diag) noexcept {
        const uint32_t before = diag.IterationCount();
        const uint32_t target = before + 2U;        // two ticks for safety
        for (uint32_t spins = 0; spins < 200U; ++spins) {
            if (diag.IterationCount() >= target) return;
            os_delay_msec(20);
        }
    }

    static void PromptOperator(const SelfTestPlan& plan,
                               const char* family,
                               std::string_view valve_label,
                               std::string_view model_label) noexcept {
        if (plan.prompt_wait_ms == 0U) return;
        ESP_LOGW(kTag,
                 "── Connect ONLY %s '%.*s' (%.*s) — test starts in %lu ms ──",
                 family,
                 static_cast<int>(valve_label.size()), valve_label.data(),
                 static_cast<int>(model_label.size()), model_label.data(),
                 static_cast<unsigned long>(plan.prompt_wait_ms));
        os_delay_msec(plan.prompt_wait_ms);
    }

    static void DwellBetweenValves(const SelfTestPlan& plan) noexcept {
        if (plan.inter_valve_pause_ms == 0U) return;
        ESP_LOGI(kTag, "  …pausing %lu ms before next valve…",
                 static_cast<unsigned long>(plan.inter_valve_pause_ms));
        os_delay_msec(plan.inter_valve_pause_ms);
    }

    static void LogSolenoidSnapshot(std::string_view label, SolenoidValve& v) noexcept {
        const auto s = v.Snapshot();
        ESP_LOGI(kTag,
                 "    %.*s: state=%-12s tgt=%.2f act=%.2f  flags=[ocp=%d hhf=%d olf=%d dpm=%d] fc=%lu",
                 static_cast<int>(label.size()), label.data(),
                 ValveStateToString(v.GetState()),
                 static_cast<double>(v.GetTargetPosition()),
                 static_cast<double>(v.GetActualPosition()),
                 s.overcurrent, s.hit_not_reached,
                 s.open_load,   s.plunger_movement_fault,
                 static_cast<unsigned long>(s.fault_count));
    }


    static void FormatSolenoidFaults(const SolenoidDiagSnapshot& s,
                                     uint32_t apply_errors,
                                     std::array<char, 128>& out) noexcept {
        std::snprintf(out.data(), out.size(),
                      "apply_errs=%lu fc=%lu  ocp=%d hhf=%d olf=%d dpm=%d",
                      static_cast<unsigned long>(apply_errors),
                      static_cast<unsigned long>(s.fault_count),
                      s.overcurrent, s.hit_not_reached,
                      s.open_load,   s.plunger_movement_fault);
    }

    static void FormatProportionalFaults(const ProportionalDiagSnapshot& s,
                                         uint32_t apply_errors,
                                         hf_valve_err_t last_err,
                                         const SelfTestResult& tr,
                                         std::array<char, 128>& out) noexcept {
        std::snprintf(out.data(), out.size(),
                      "apply_errs=%lu fc=%lu last=%s  trk[out=%lu/%lu max=%dmA "
                      "↑=%d ↓=%d]  oc=%d sg=%d ol=%d ot=%d olsg=%d",
                      static_cast<unsigned long>(apply_errors),
                      static_cast<unsigned long>(s.fault_count),
                      HfValveErrToString(last_err),
                      static_cast<unsigned long>(tr.tracking_outliers),
                      static_cast<unsigned long>(tr.tracking_samples),
                      static_cast<int>(tr.tracking_max_abs_err_mA),
                      tr.ramp_monotonic_up, tr.ramp_monotonic_down,
                      s.overcurrent, s.short_to_ground, s.open_load,
                      s.over_temperature, s.open_load_short_ground);
    }

    static void PrintSummary(const SelfTestReport& report) noexcept {
        ESP_LOGI(kTag, "");
        ESP_LOGI(kTag, "════════════════════════════════════════════════════════════");
        ESP_LOGI(kTag, "  WH Valve Self-Test Summary");
        ESP_LOGI(kTag, "════════════════════════════════════════════════════════════");
        ESP_LOGI(kTag, "  %-8s %-12s %-6s  %s", "Type", "Valve", "Pass", "Notes");
        ESP_LOGI(kTag, "  %-8s %-12s %-6s  %s", "----", "-----", "----", "-----");

        for (std::size_t i = 0; i < kNumSolenoids; ++i) {
            const auto& r = report.solenoids[i];
            ESP_LOGI(kTag, "  %-8s %-12.*s %-6s  %s",
                     "SOL",
                     static_cast<int>(r.label.size()), r.label.data(),
                     r.pass ? "OK" : "FAIL",
                     r.pass ? "" : r.fault_summary.data());
        }
        for (std::size_t i = 0; i < kNumProportionals; ++i) {
            const auto& r = report.proportionals[i];
            // For proportionals, print the per-step tracking metrics inline
            // even on PASS so the operator gets a quick sense of headroom.
            char buf[160];
            std::snprintf(buf, sizeof(buf),
                          "trk[out=%lu/%lu max=%dmA mean=%+dmA ↑=%d ↓=%d]  %s",
                          static_cast<unsigned long>(r.tracking_outliers),
                          static_cast<unsigned long>(r.tracking_samples),
                          static_cast<int>(r.tracking_max_abs_err_mA),
                          static_cast<int>(r.tracking_mean_err_mA),
                          r.ramp_monotonic_up, r.ramp_monotonic_down,
                          r.pass ? "" : r.fault_summary.data());
            ESP_LOGI(kTag, "  %-8s %-12.*s %-6s  %s",
                     "PROP",
                     static_cast<int>(r.label.size()), r.label.data(),
                     r.pass ? "OK" : "FAIL",
                     buf);
        }
        ESP_LOGI(kTag, "");
        ESP_LOGI(kTag, "  OVERALL: %s",
                 report.overall_pass ? "PASS" : "FAIL — see entries above");
        ESP_LOGI(kTag, "════════════════════════════════════════════════════════════");
        ESP_LOGI(kTag, "");
    }
};

}  // namespace whvalve
