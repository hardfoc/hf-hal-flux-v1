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

#include <algorithm>
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

    //--------------------------------------------------------------------
    // Proportional step-response characterization
    //--------------------------------------------------------------------
    //
    // Each step of the 0 → full → 0 ladder is exercised in two phases:
    //
    //   1) Settling: poll the chip's FB_I_AVG every `prop_sample_period_ms`
    //      until the actual current stays within `±settling_band` for
    //      `settling_consec_samples` consecutive reads, or until
    //      `settling_max_ms` elapses (timeout = "did not settle"). The
    //      worst |err| seen during this phase is recorded as
    //      `overshoot_max_mA`.
    //
    //   2) Steady-state: hold the setpoint for `steady_state_ms` and
    //      accumulate sum / sum_of_squares / max-|err|. Compute
    //      mean error and standard deviation; check both against
    //      `accuracy_band_*` and `noise_floor_mA`.
    //
    // This separates the valve's *dynamic* response (settling time +
    // overshoot — physics) from its *static* accuracy (mean error + noise —
    // chip + sensor). LM-Pro settles in ~30 ms, slow VSO solenoids in
    // ~80–200 ms; using a single fixed snapshot would either fail on the
    // slow ones (snapshot mid-transient) or paper over real settling
    // problems (snapshot way after settling). Two-phase accounting catches
    // both classes of failure cleanly.

    /// Number of intermediate setpoints in the 0 → full → 0 ladder. Total
    /// visited setpoints = 2 × steps + 1. Keep modest (5–10) so each step
    /// has time for full settle + steady-state characterization.
    uint32_t proportional_steps{6};

    /// Sampling cadence inside both settling and steady-state phases (ms).
    /// 20 ms = 50 Hz which still gives 5 fresh diagnostic-thread sweeps
    /// inside a 100 ms steady-state window.
    uint32_t prop_sample_period_ms{20};

    /// Settling acceptance band: actual must stay within
    /// `max(settling_band_min_mA, settling_band_pct% × full_flow)` for
    /// `settling_consec_samples` consecutive reads to declare "settled".
    float    settling_band_pct{5.0f};
    float    settling_band_min_mA{3.0f};
    uint32_t settling_consec_samples{3};

    /// Hard cap on settling time before the step is flagged as
    /// "did not settle". Tune to your slowest valve family — VSO LowPro
    /// can take ~150 ms for a full-scale step.
    uint32_t settling_max_ms{300};

    /// How long to characterize steady state once a step has settled (ms).
    /// 200 ms at 20 ms cadence = 10 samples per step, plenty for a stable
    /// mean / stddev. Drop to 100 ms for a faster sweep.
    uint32_t steady_state_ms{200};

    /// Steady-state accuracy band for |mean_err| pass/fail.
    float    accuracy_band_pct{5.0f};
    float    accuracy_band_min_mA{3.0f};

    /// Steady-state noise budget — `stddev` above this fails the step.
    /// LM-Pro typically reads ~3 mA stddev at 80 mA setpoint; VSO families
    /// run a bit lower thanks to their dither.
    float    noise_floor_mA{5.0f};

    /// Maximum number of steps in the full ladder that may fail any of the
    /// {settled, accurate, low-noise} checks before the whole valve is
    /// marked FAIL. ≥ 1 absorbs the very-low-end noise pass that the chip
    /// can't reach (e.g. setpoint 0 mA reads back -2 mA).
    uint32_t prop_max_failed_steps{1};

    //--------------------------------------------------------------------
    // Legacy per-step tracking knobs (used only by the original
    // TestProportional triangle ramp; new step-response code uses the
    // settling_* / accuracy_* / noise_floor_mA knobs above).
    //--------------------------------------------------------------------
    /// Settle time at each ramp step before sampling (ms).
    uint32_t proportional_settle_ms{80};
    /// Tracking band: ±max(tracking_band_min_mA, tracking_band_pct% × full_flow).
    float    tracking_band_pct{5.0f};
    float    tracking_band_min_mA{2.0f};
    /// Max ramp-step outliers tolerated before per-valve test fails.
    uint32_t tracking_max_outliers{2};

    //--------------------------------------------------------------------
    // Per-valve proportional sweep (one valve at a time)
    //--------------------------------------------------------------------
    /// When true the per-valve proportional triangle is run after the
    /// solenoid sweep. When false only the parallel stress test is run.
    /// On a manifold-limited bench where moving the manifold connection
    /// between every TLE channel is impractical, set this to false and
    /// rely on the parallel stress test instead.
    bool enable_per_valve_proportional{false};

    //--------------------------------------------------------------------
    // All-proportionals parallel stress test (worst-case driver load)
    //--------------------------------------------------------------------
    //
    // Drives every TLE92466ED channel simultaneously through a synchronized
    // step ladder. Catches what a per-valve sweep cannot:
    //   - VBAT droop under simultaneous high current draw (sum across
    //     all 6 channels can exceed 600 mA, ~14 W into the load),
    //   - SPI saturation (every dirty Apply pushes a setpoint write),
    //   - Central thermal warnings (`tle_ot_warning`),
    //   - Per-channel current-regulation / pwm-regulation warnings that
    //     only show up when the PSU can't keep up,
    //   - Driver state-machine races between Configuration-Mode round-trips
    //     happening on different channels.
    //
    // Two patterns are run back-to-back:
    //   1. UNIFORM   — every channel at the same normalized position; walks
    //                  0 → 33 → 66 → 100 → 66 → 33 → 0. This is the
    //                  worst-case current-draw scenario.
    //   2. STAGGERED — at each step, channel i gets level[(step + i) %
    //                  num_levels]. After num_levels steps every channel
    //                  has visited every level; tests realistic phase-
    //                  staggered control behaviour (different VBAT
    //                  droop / regulation profile than uniform).
    //
    // The stress test enforces explicit pass/fail tolerances (below) and
    // its verdict feeds into the overall self-test result. Set
    // `parallel_stress_diagnostic_only = true` to revert to the original
    // behaviour where the stress test only logs and never fails (useful
    // during commissioning / first light).
    bool     enable_parallel_stress_test{true};
    bool     parallel_stress_run_uniform{true};
    bool     parallel_stress_run_staggered{true};
    bool     parallel_stress_diagnostic_only{false};

    /// Settle wait per stress-test level (ms). 500 ms covers the slowest
    /// VSO solenoid family at full current under simultaneous load (which
    /// briefly droops VBAT and lengthens the chip's CDR settling time
    /// compared to a single-channel test). LM-Pro can still shortfall
    /// even at 500 ms when total simultaneous current crosses the bench
    /// PSU's regulation envelope — that's a real failure to be flagged
    /// by the tolerance band, not a settling artifact.
    uint32_t parallel_settle_ms{500};
    /// Steady-state characterization window per stress level (ms).
    uint32_t parallel_steady_state_ms{200};
    /// Sampling cadence inside the stress test (ms).
    uint32_t parallel_sample_period_ms{20};
    /// Pause between the uniform and staggered passes so VBAT can recover.
    uint32_t parallel_inter_pattern_pause_ms{1500};

    //--------------------------------------------------------------------
    // Parallel-stress PASS/FAIL tolerances
    //--------------------------------------------------------------------
    //
    // For each (channel × stress level) we evaluate three sub-checks:
    //
    //   |mean_err|  ≤ max(max_abs_mean_err_min_mA,
    //                     max_abs_mean_err_pct% × cmd_mA)
    //   stddev      ≤ max_stddev_mA
    //   |excursion| ≤ max(max_excursion_min_mA,
    //                     max_excursion_pct% × cmd_mA)
    //
    // A step "fails" if any sub-check fails OR if the channel's snapshot
    // shows a hard fault flag. Per-channel pass requires that the number
    // of failed steps is ≤ `parallel_max_failed_steps_per_channel` AND no
    // forbidden warning flag was raised. Pattern pass requires every
    // channel pass AND none of the subsystem fail flags below were seen.
    // Stress test passes (and feeds into overall_pass) only when every
    // executed pattern passes — unless `parallel_stress_diagnostic_only`
    // is true, in which case the verdict is logged but does not lower
    // overall_pass.
    //
    // Defaults are tuned for the WH bench: VSO valves typically settle
    // within ±5 mA of setpoint under simultaneous load; LM-Pro can
    // shortfall ~20 mA at full simultaneous current draw (494 mA total)
    // because of VBAT droop into the load. The 15 % / 6 mA threshold
    // catches "no current at all" and "wildly off" but tolerates that
    // expected shortfall.
    float    parallel_max_abs_mean_err_pct{15.0f};
    float    parallel_max_abs_mean_err_min_mA{6.0f};
    float    parallel_max_stddev_mA{10.0f};
    float    parallel_max_excursion_pct{25.0f};
    float    parallel_max_excursion_min_mA{12.0f};
    /// Per channel: tolerate this many failed steps before flagging the
    /// channel as a stress-test failure. ≥ 1 absorbs the very-low-end
    /// noise step (cmd ≈ 0 with chip offset of -2 mA).
    uint32_t parallel_max_failed_steps_per_channel{1};
    /// Per-channel current_regulation_warning / pwm_regulation_warning is
    /// expected when the load briefly limits a channel; tolerated by
    /// default. open_load / overcurrent / short-to-ground are caught via
    /// the hard fault path regardless of this flag.
    bool     parallel_allow_per_channel_warnings{true};

    /// Subsystem-level fail conditions (any single occurrence fails the
    /// whole pattern). vbat_uv and ot_err are always treated as faults
    /// (medical-device safety); ot_warn / clock_fault are configurable.
    bool     parallel_fail_on_vbat_uv{true};
    bool     parallel_fail_on_ot_warn{false};
    bool     parallel_fail_on_ot_err{true};
    bool     parallel_fail_on_clock_fault{true};

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

    // ── Proportional step-response characterization (parallel stress) ──
    // All zero for solenoids and unused when the per-valve sweep is off.
    uint32_t steps_executed{0};            ///< Total setpoint steps walked.
    uint32_t steps_failed{0};              ///< Steps that failed any sub-check.
    uint32_t steps_did_not_settle{0};      ///< Steps that hit `settling_max_ms`.

    // Aggregate dynamic-response metrics across the whole ladder.
    uint16_t worst_settling_ms{0};         ///< Max settling_time across all steps.
    int16_t  worst_overshoot_mA{0};        ///< Max |err| seen during any settle phase.

    // Aggregate steady-state metrics across the whole ladder.
    int16_t  worst_mean_err_mA{0};         ///< Max |mean_err| at any held step.
    int16_t  worst_stddev_mA{0};           ///< Max stddev at any held step.

    // ── Legacy per-step triangle-ramp metrics (used by TestProportional) ──
    uint32_t tracking_samples{0};          ///< How many setpoints were measured.
    uint32_t tracking_outliers{0};         ///< How many fell outside the ±band.
    int16_t  tracking_max_abs_err_mA{0};   ///< Worst |setpoint − actual|.
    int16_t  tracking_mean_err_mA{0};      ///< Mean (signed) error across the ramp.

    bool     ramp_monotonic_up{true};      ///< Up-ramp steady-state values strictly ↑.
    bool     ramp_monotonic_down{true};    ///< Down-ramp steady-state values strictly ↓.

    /// Aggregated, human-readable description of every fault flag seen.
    /// Empty when `pass == true`. Kept small to fit in the main-task stack.
    std::array<char, 128> fault_summary{};
};

/**
 * @brief Per-channel observation captured during a parallel stress sweep.
 *
 * One row per (proportional channel × stress level visited). The stress
 * test prints these inline as it runs and aggregates a worst-case summary
 * per channel for the final report.
 */
struct ParallelChannelObservation {
    int16_t  cmd_mA{0};                     ///< Setpoint at this level.
    int16_t  mean_actual_mA{0};             ///< Mean steady-state current.
    int16_t  mean_err_mA{0};                ///< mean_actual - cmd.
    int16_t  stddev_mA{0};                  ///< Steady-state stddev (mA).
    int16_t  max_excursion_mA{0};           ///< Max |actual - cmd| during steady state.
    bool     any_fault{false};              ///< Any per-channel fault flag set.
    bool     any_warning{false};            ///< Any per-channel warning flag set.
};

/**
 * @brief Per-pattern (uniform / staggered) parallel-stress result for one
 *        proportional channel.
 */
struct ParallelChannelSummary {
    std::string_view label;
    ValveChannel     channel{};
    int16_t          worst_mean_err_mA{0};      ///< Max |mean_err| across levels.
    int16_t          worst_stddev_mA{0};        ///< Max stddev across levels.
    int16_t          worst_excursion_mA{0};     ///< Max |excursion| across levels.
    uint32_t         apply_errors{0};
    uint32_t         fault_count{0};            ///< Sticky from snapshot.
    bool             any_fault_seen{false};
    bool             any_warning_seen{false};

    // PASS evaluation
    uint32_t         steps_failed{0};           ///< Per-step sub-check failures.
    bool             pass{true};                ///< False if too many step failures
                                                ///< OR a hard fault / disallowed warning.
    /// One-line reason for failure, empty when pass==true.
    std::array<char, 96> fail_reason{};
};

/**
 * @brief Aggregated parallel stress sweep result for one pattern.
 */
struct ParallelStressPatternResult {
    bool     executed{false};
    /// Subsystem-level worst-case observations across the sweep.
    uint16_t worst_diag_iterations{0};          ///< For sanity / debugging.
    bool     vbat_under_voltage_seen{false};
    bool     ot_warning_seen{false};
    bool     ot_error_seen{false};
    bool     clock_fault_seen{false};
    /// One row per proportional channel.
    std::array<ParallelChannelSummary, kNumProportionals> channels{};
    /// Aggregated PASS verdict across all channels + subsystem flags.
    bool     pass{true};
    /// Brief reason for failure ("vbat_uv seen" / "ot_err seen" / "channel
    /// XYZ failed"). Empty on pass. Diagnostic-only is reported here too.
    std::array<char, 128> fail_reason{};
};

/// Aggregated self-test report.
struct SelfTestReport {
    std::array<SelfTestResult, kNumSolenoids>     solenoids{};
    std::array<SelfTestResult, kNumProportionals> proportionals{};
    ParallelStressPatternResult stress_uniform{};
    ParallelStressPatternResult stress_staggered{};
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

        // ── Per-valve proportional sweep (optional, default OFF) ─────────
        //
        // Original mode: walk each TLE channel one valve at a time, ramping
        // 0 → full → 0 with per-step tracking checks. Useful when the
        // operator can attach a single proportional valve to the manifold.
        // Disabled by default (`enable_per_valve_proportional = false`) on
        // the WH bench because it's impractical to physically swap the
        // manifold connection between every channel — the parallel stress
        // test (below) gives equivalent coverage in one shot.
        if (plan.enable_per_valve_proportional) {
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
        } else {
            // Mark per-valve proportional rows as "skipped — parallel only"
            // so the summary block stays consistent.
            for (std::size_t i = 0; i < kNumProportionals; ++i) {
                report.proportionals[i].label   = kProportionalMap[i].label;
                report.proportionals[i].channel = kProportionalMap[i].manager_channel;
                report.proportionals[i].pass    = true;  // skipped == not failed
            }
        }

        // ── All-proportionals parallel stress test (worst-case load) ────
        if (plan.enable_parallel_stress_test) {
            if (plan.parallel_stress_run_uniform) {
                report.stress_uniform =
                    RunParallelStress(system, diag, plan, /*staggered=*/false);
            }
            if (plan.parallel_stress_run_staggered) {
                if (plan.parallel_stress_run_uniform &&
                    plan.parallel_inter_pattern_pause_ms > 0) {
                    ESP_LOGI(kTag,
                             "  …pausing %lu ms between UNIFORM and STAGGERED so VBAT recovers…",
                             static_cast<unsigned long>(plan.parallel_inter_pattern_pause_ms));
                    os_delay_msec(plan.parallel_inter_pattern_pause_ms);
                }
                report.stress_staggered =
                    RunParallelStress(system, diag, plan, /*staggered=*/true);
            }

            // Stress verdicts feed into overall_pass unless the operator
            // explicitly opted into diagnostic-only mode (commissioning).
            if (!plan.parallel_stress_diagnostic_only) {
                if (report.stress_uniform.executed && !report.stress_uniform.pass) {
                    report.overall_pass = false;
                }
                if (report.stress_staggered.executed && !report.stress_staggered.pass) {
                    report.overall_pass = false;
                }
            }
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
    // Parallel stress test: drive every TLE92466ED channel simultaneously
    //---------------------------------------------------------------------
    static ParallelStressPatternResult
    RunParallelStress(WhValveSystem&         system,
                      WhDiagnosticsThread&   diag,
                      const SelfTestPlan&    plan,
                      bool                   staggered) noexcept {
        ParallelStressPatternResult result{};
        result.executed = true;
        for (std::size_t i = 0; i < kNumProportionals; ++i) {
            result.channels[i].label   = kProportionalMap[i].label;
            result.channels[i].channel = kProportionalMap[i].manager_channel;
        }

        // Step ladder used for both patterns. "Triangle" so each channel
        // visits every level twice and we get up + down dynamics. Values
        // expressed as normalized [0..1] positions; the actual mA pushed
        // is `pos × full_flow_current_ma` per channel (so VSO gets 0..66 mA
        // while LM-Pro gets 0..115 mA — different stress per family).
        constexpr std::array<float, 7> kLevels{
            0.00f, 0.33f, 0.66f, 1.00f, 0.66f, 0.33f, 0.00f
        };

        const char* pattern_name = staggered ? "STAGGERED" : "UNIFORM";
        ESP_LOGI(kTag, "");
        ESP_LOGI(kTag, "════════════════════════════════════════════════════════════");
        ESP_LOGI(kTag, "  Parallel Stress Sweep — pattern=%s  6 channels driven simultaneously",
                 pattern_name);
        ESP_LOGI(kTag, "  per-step settle=%lu ms, steady-state=%lu ms, sample=%lu ms",
                 static_cast<unsigned long>(plan.parallel_settle_ms),
                 static_cast<unsigned long>(plan.parallel_steady_state_ms),
                 static_cast<unsigned long>(plan.parallel_sample_period_ms));
        ESP_LOGI(kTag, "════════════════════════════════════════════════════════════");

        // Snapshot per-channel apply-error / fault counters before we start
        // so the stress test can report deltas instead of cumulative totals.
        std::array<uint32_t, kNumProportionals> apply_errs_before{};
        std::array<uint32_t, kNumProportionals> fault_count_before{};
        for (std::size_t i = 0; i < kNumProportionals; ++i) {
            auto& v = system.Get(static_cast<ProportionalId>(i));
            apply_errs_before[i]  = v.GetApplyErrorCount();
            fault_count_before[i] = v.GetFaultCount();
        }

        const std::size_t num_levels = kLevels.size();
        // For uniform: walk all levels once. For staggered: walk num_levels
        // steps where channel i sees level[(step + i) % num_levels] — after
        // one full rotation every channel has seen every level.
        const std::size_t num_steps = num_levels;

        for (std::size_t step = 0; step < num_steps; ++step) {
            // ── 1) Push setpoints to every channel ─────────────────────
            for (std::size_t i = 0; i < kNumProportionals; ++i) {
                const float pos = staggered
                    ? kLevels[(step + i) % num_levels]
                    : kLevels[step];
                auto& v = system.Get(static_cast<ProportionalId>(i));
                v.RequestPosition(pos);
            }

            // ── 2) Settle wait ─────────────────────────────────────────
            os_delay_msec(plan.parallel_settle_ms);
            WaitForDiagPass(diag);

            // ── 3) Steady-state characterization per channel ────────────
            //
            // Sample every `parallel_sample_period_ms` for the configured
            // window. Per channel: accumulate sum and sum_of_squares for a
            // running mean+stddev, plus track max |excursion|. Per
            // subsystem: track the worst SubsystemFaultSnapshot flags seen.
            std::array<int64_t,  kNumProportionals>  sum{};
            std::array<int64_t,  kNumProportionals>  sum_sq{};
            std::array<uint16_t, kNumProportionals>  n{};
            std::array<int16_t,  kNumProportionals>  max_excursion{};
            std::array<bool,     kNumProportionals>  step_any_fault{};
            std::array<bool,     kNumProportionals>  step_any_warn{};

            // Cache the commanded mA per channel for this step.
            std::array<int16_t, kNumProportionals> step_cmd{};
            for (std::size_t i = 0; i < kNumProportionals; ++i) {
                const float pos = staggered
                    ? kLevels[(step + i) % num_levels]
                    : kLevels[step];
                step_cmd[i] = static_cast<int16_t>(
                    pos *
                    static_cast<float>(kProportionalMap[i].model.full_flow_current_ma) +
                    0.5f);
            }

            const uint32_t window_steps =
                (plan.parallel_steady_state_ms + plan.parallel_sample_period_ms - 1U) /
                plan.parallel_sample_period_ms;
            for (uint32_t s = 0; s < window_steps; ++s) {
                os_delay_msec(plan.parallel_sample_period_ms);
                for (std::size_t i = 0; i < kNumProportionals; ++i) {
                    auto& v = system.Get(static_cast<ProportionalId>(i));
                    const auto snap = v.Snapshot();
                    const int16_t actual = snap.average_current_mA;
                    const int16_t err    = static_cast<int16_t>(actual - step_cmd[i]);
                    const int16_t abs_e  = static_cast<int16_t>(err < 0 ? -err : err);
                    sum[i]     += actual;
                    sum_sq[i]  += static_cast<int64_t>(actual) * actual;
                    if (abs_e > max_excursion[i]) max_excursion[i] = abs_e;
                    if (snap.any_fault())   step_any_fault[i] = true;
                    if (snap.any_warning()) step_any_warn[i]  = true;
                    ++n[i];
                }
                // Subsystem-level scrape every sample for VBAT droop / OT.
                const auto sub = diag.GetSnapshot();
                if (sub.tle_vbat_uv) result.vbat_under_voltage_seen = true;
                if (sub.tle_ot_warning) result.ot_warning_seen = true;
                if (sub.tle_ot_error)   result.ot_error_seen   = true;
                if (sub.tle_clock_fault) result.clock_fault_seen = true;
            }

            // ── 4) Log per-channel observation for this step ───────────
            ESP_LOGI(kTag,
                     "  step %u/%u  pattern=%s",
                     static_cast<unsigned>(step + 1U),
                     static_cast<unsigned>(num_steps),
                     pattern_name);
            for (std::size_t i = 0; i < kNumProportionals; ++i) {
                ParallelChannelObservation obs{};
                obs.cmd_mA        = step_cmd[i];
                obs.max_excursion_mA = max_excursion[i];
                obs.any_fault     = step_any_fault[i];
                obs.any_warning   = step_any_warn[i];
                if (n[i] > 0) {
                    const int64_t mean = sum[i] / n[i];
                    obs.mean_actual_mA = static_cast<int16_t>(mean);
                    obs.mean_err_mA    = static_cast<int16_t>(mean - step_cmd[i]);
                    // stddev = sqrt(E[X²] − E[X]²)
                    const int64_t var = (sum_sq[i] / n[i]) - (mean * mean);
                    const int32_t v32 = (var < 0) ? 0 : static_cast<int32_t>(var);
                    obs.stddev_mA = IsqrtClamped(v32);
                }
                // ── Evaluate per-step pass against tolerances ───────────
                const float abs_cmd_f = static_cast<float>(
                    obs.cmd_mA < 0 ? -obs.cmd_mA : obs.cmd_mA);
                const float band_err  = std::max(
                    plan.parallel_max_abs_mean_err_min_mA,
                    (plan.parallel_max_abs_mean_err_pct * abs_cmd_f) / 100.0f);
                const float band_exc  = std::max(
                    plan.parallel_max_excursion_min_mA,
                    (plan.parallel_max_excursion_pct * abs_cmd_f) / 100.0f);

                const int16_t abs_mean_err =
                    obs.mean_err_mA < 0 ? -obs.mean_err_mA : obs.mean_err_mA;

                const bool ok_err  = static_cast<float>(abs_mean_err)        <= band_err;
                const bool ok_sd   = static_cast<float>(obs.stddev_mA)       <= plan.parallel_max_stddev_mA;
                const bool ok_exc  = static_cast<float>(obs.max_excursion_mA) <= band_exc;
                const bool ok_warn = !obs.any_warning ||
                                     plan.parallel_allow_per_channel_warnings;
                const bool step_ok = ok_err && ok_sd && ok_exc &&
                                     !obs.any_fault && ok_warn;

                ESP_LOGI(kTag,
                         "    %-6.*s  cmd=%4d  mean=%4d  err=%+4d  σ=%3d  "
                         "|exc|=%3d  fault=%d  warn=%d  %s",
                         static_cast<int>(result.channels[i].label.size()),
                         result.channels[i].label.data(),
                         static_cast<int>(obs.cmd_mA),
                         static_cast<int>(obs.mean_actual_mA),
                         static_cast<int>(obs.mean_err_mA),
                         static_cast<int>(obs.stddev_mA),
                         static_cast<int>(obs.max_excursion_mA),
                         obs.any_fault ? 1 : 0,
                         obs.any_warning ? 1 : 0,
                         step_ok ? "OK" : "FAIL");

                // ── Aggregate per channel ───────────────────────────────
                auto& ch = result.channels[i];
                if (abs_mean_err > ch.worst_mean_err_mA)
                    ch.worst_mean_err_mA = abs_mean_err;
                if (obs.stddev_mA > ch.worst_stddev_mA)
                    ch.worst_stddev_mA = obs.stddev_mA;
                if (obs.max_excursion_mA > ch.worst_excursion_mA)
                    ch.worst_excursion_mA = obs.max_excursion_mA;
                if (obs.any_fault)   ch.any_fault_seen   = true;
                if (obs.any_warning) ch.any_warning_seen = true;
                if (!step_ok)        ++ch.steps_failed;
            }
        }

        // ── Wind down: drive everything back to 0 ──────────────────────
        for (std::size_t i = 0; i < kNumProportionals; ++i) {
            system.Get(static_cast<ProportionalId>(i)).RequestPosition(0.0f);
        }
        os_delay_msec(plan.parallel_settle_ms);
        WaitForDiagPass(diag);

        // ── Snapshot delta per-channel apply-error / fault counters ────
        for (std::size_t i = 0; i < kNumProportionals; ++i) {
            auto& v = system.Get(static_cast<ProportionalId>(i));
            result.channels[i].apply_errors =
                v.GetApplyErrorCount() - apply_errs_before[i];
            result.channels[i].fault_count =
                v.GetFaultCount() - fault_count_before[i];
        }

        result.worst_diag_iterations = static_cast<uint16_t>(diag.IterationCount());

        // ── Roll up per-channel pass + pattern pass ────────────────────
        result.pass = true;
        std::array<char, 96> first_failure{};
        bool have_first_failure = false;

        // Subsystem-level fail conditions.
        const auto note_subsystem_fail = [&](const char* what) {
            if (!have_first_failure) {
                std::snprintf(result.fail_reason.data(), result.fail_reason.size(),
                              "subsystem: %s", what);
                have_first_failure = true;
            }
            result.pass = false;
        };
        if (plan.parallel_fail_on_vbat_uv && result.vbat_under_voltage_seen)
            note_subsystem_fail("vbat_uv seen");
        if (plan.parallel_fail_on_ot_warn && result.ot_warning_seen)
            note_subsystem_fail("ot_warning seen");
        if (plan.parallel_fail_on_ot_err && result.ot_error_seen)
            note_subsystem_fail("ot_error seen");
        if (plan.parallel_fail_on_clock_fault && result.clock_fault_seen)
            note_subsystem_fail("clock_fault seen");

        // Per-channel rollup.
        for (std::size_t i = 0; i < kNumProportionals; ++i) {
            auto& ch = result.channels[i];
            ch.pass = true;
            // Forbidden warnings / hard faults / apply errors.
            const bool warn_blocks =
                ch.any_warning_seen && !plan.parallel_allow_per_channel_warnings;
            if (ch.any_fault_seen)   { ch.pass = false; }
            if (ch.apply_errors > 0) { ch.pass = false; }
            if (ch.fault_count > 0)  { ch.pass = false; }
            if (warn_blocks)         { ch.pass = false; }
            if (ch.steps_failed > plan.parallel_max_failed_steps_per_channel) {
                ch.pass = false;
            }
            if (!ch.pass) {
                std::snprintf(ch.fail_reason.data(), ch.fail_reason.size(),
                              "fail_steps=%lu/%lu fault=%d warn=%d apply_err=%lu fc=%lu",
                              static_cast<unsigned long>(ch.steps_failed),
                              static_cast<unsigned long>(num_steps),
                              ch.any_fault_seen ? 1 : 0,
                              ch.any_warning_seen ? 1 : 0,
                              static_cast<unsigned long>(ch.apply_errors),
                              static_cast<unsigned long>(ch.fault_count));
                if (!have_first_failure) {
                    std::snprintf(result.fail_reason.data(), result.fail_reason.size(),
                                  "%.*s: %s",
                                  static_cast<int>(ch.label.size()), ch.label.data(),
                                  ch.fail_reason.data());
                    have_first_failure = true;
                }
                result.pass = false;
            }
        }

        ESP_LOGI(kTag, "  pattern=%s complete — verdict=%s%s%s",
                 pattern_name,
                 result.pass ? "PASS" : "FAIL",
                 result.pass ? "" : " (",
                 result.pass ? "" : result.fail_reason.data());
        if (!result.pass) ESP_LOGE(kTag, "  ↑ pattern=%s FAILED", pattern_name);
        ESP_LOGI(kTag, "");
        return result;
    }

    /// Integer sqrt for variance → stddev (avoids pulling <cmath> for one call).
    static int16_t IsqrtClamped(int32_t v) noexcept {
        if (v <= 0) return 0;
        // Newton's method, ~10 iterations is more than enough for int32_t.
        int32_t x = v;
        int32_t y = (x + 1) / 2;
        while (y < x) {
            x = y;
            y = (x + v / x) / 2;
        }
        return (x > INT16_MAX) ? INT16_MAX : static_cast<int16_t>(x);
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
            // The per-valve sweep is optional. If it didn't run, the
            // proportional rows are filled with `pass=true` placeholders
            // and zeroed metrics — show that explicitly rather than
            // pretending they "PASSED" with all zeros.
            if (r.tracking_samples == 0) {
                ESP_LOGI(kTag, "  %-8s %-12.*s %-6s  %s",
                         "PROP",
                         static_cast<int>(r.label.size()), r.label.data(),
                         "SKIP",
                         "per-valve sweep disabled — see PARALLEL STRESS below");
                continue;
            }
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

        // ── Parallel stress sweeps ──────────────────────────────────────
        PrintParallelStress("UNIFORM",   report.stress_uniform);
        PrintParallelStress("STAGGERED", report.stress_staggered);

        ESP_LOGI(kTag, "");
        ESP_LOGI(kTag, "  OVERALL: %s",
                 report.overall_pass ? "PASS" : "FAIL — see entries above");
        ESP_LOGI(kTag, "════════════════════════════════════════════════════════════");
        ESP_LOGI(kTag, "");
    }

    static void PrintParallelStress(const char* pattern,
                                    const ParallelStressPatternResult& r) noexcept {
        if (!r.executed) return;
        ESP_LOGI(kTag, "");
        ESP_LOGI(kTag, "  ── PARALLEL STRESS — pattern=%s — %s ──",
                 pattern, r.pass ? "PASS" : "FAIL");
        ESP_LOGI(kTag, "    subsystem flags seen: vbat_uv=%d ot_warn=%d ot_err=%d clk_fault=%d",
                 r.vbat_under_voltage_seen, r.ot_warning_seen,
                 r.ot_error_seen, r.clock_fault_seen);
        if (!r.pass) {
            ESP_LOGE(kTag, "    fail reason: %s", r.fail_reason.data());
        }
        ESP_LOGI(kTag, "    %-6s  %-4s  %-9s  %-9s  %-9s  %-7s  %-7s  %-7s  %s",
                 "Valve", "Pass", "max|err|", "max σ", "max|exc|",
                 "fail/N", "applyE", "fault++", "obs");
        for (std::size_t i = 0; i < kNumProportionals; ++i) {
            const auto& ch = r.channels[i];
            char obs[48];
            std::snprintf(obs, sizeof(obs),
                          "fault=%d warn=%d",
                          ch.any_fault_seen ? 1 : 0,
                          ch.any_warning_seen ? 1 : 0);
            ESP_LOGI(kTag,
                     "    %-6.*s  %-4s  %4dmA      %4dmA      %4dmA      %5lu     %5lu     %5lu     %s",
                     static_cast<int>(ch.label.size()), ch.label.data(),
                     ch.pass ? "OK" : "FAIL",
                     static_cast<int>(ch.worst_mean_err_mA),
                     static_cast<int>(ch.worst_stddev_mA),
                     static_cast<int>(ch.worst_excursion_mA),
                     static_cast<unsigned long>(ch.steps_failed),
                     static_cast<unsigned long>(ch.apply_errors),
                     static_cast<unsigned long>(ch.fault_count),
                     obs);
            if (!ch.pass && ch.fail_reason[0]) {
                ESP_LOGE(kTag, "      ↳ %s", ch.fail_reason.data());
            }
        }
    }
};

}  // namespace whvalve
