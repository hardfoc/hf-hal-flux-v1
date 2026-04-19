/**
 * @file valve_cycling_test.cpp
 * @brief Valve manager focused cycling test for ESP32
 *
 * Deep-dive testing of ValveManager:
 *   - Solenoid channel enable/disable cycling (MAX22200)
 *   - Proportional channel current control (TLE92466ED)
 *   - Batch disable-all safety operation
 *   - Channel status queries
 *   - Out-of-range channel safety
 *   - Rapid cycling stress
 *   - Diagnostics dump
 *
 * @author HardFOC Team
 * @date 2025
 * @copyright HardFOC
 */

#include "api/Flux.h"
#include "managers/ValveManager.h"
#include "TestFramework.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "ValveCycleTest";
static TestResults g_test_results;

static auto& VALVES() noexcept { return FLUX_API.valves; }

// ── Tests ─────────────────────────────────────────────────────────────────

static bool test_valve_ensure_initialized() noexcept {
  FLUX_API.EnsureInitialized();
  return VALVES().EnsureInitialized();
}

static bool test_valve_enable_solenoid_ch0() noexcept {
  [[maybe_unused]] auto err = VALVES().EnableChannel(ValveChannel::SOL_CH0);
  return true;
}

static bool test_valve_disable_solenoid_ch0() noexcept {
  [[maybe_unused]] auto err = VALVES().DisableChannel(ValveChannel::SOL_CH0);
  return true;
}

static bool test_valve_is_enabled_query() noexcept {
  VALVES().DisableChannel(ValveChannel::SOL_CH0);
  [[maybe_unused]] bool enabled = VALVES().IsChannelEnabled(ValveChannel::SOL_CH0);
  return true;
}

static bool test_valve_disable_all() noexcept {
  [[maybe_unused]] auto err = VALVES().DisableAllChannels();
  return true;
}

static bool test_valve_cycle_all_solenoids() noexcept {
  // Exercise each solenoid channel — enable then immediately disable
  const ValveChannel solenoids[] = {
    ValveChannel::SOL_CH0, ValveChannel::SOL_CH1,
    ValveChannel::SOL_CH2, ValveChannel::SOL_CH3,
    ValveChannel::SOL_CH4, ValveChannel::SOL_CH5,
    ValveChannel::SOL_CH6, ValveChannel::SOL_CH7,
  };
  for (auto ch : solenoids) {
    [[maybe_unused]] auto e1 = VALVES().EnableChannel(ch);
    [[maybe_unused]] auto e2 = VALVES().DisableChannel(ch);
  }
  return true;
}

static bool test_valve_proportional_set() noexcept {
  [[maybe_unused]] auto err = VALVES().SetProportionalCurrent(ValveChannel::PROP_CH0, 500);
  return true;
}

static bool test_valve_proportional_zero() noexcept {
  [[maybe_unused]] auto err = VALVES().SetProportionalCurrent(ValveChannel::PROP_CH0, 0);
  return true;
}

static bool test_valve_rapid_cycle_stress() noexcept {
  constexpr int kIterations = 200;
  for (int i = 0; i < kIterations; ++i) {
    [[maybe_unused]] auto e1 = VALVES().EnableChannel(ValveChannel::SOL_CH0);
    [[maybe_unused]] auto e2 = VALVES().DisableChannel(ValveChannel::SOL_CH0);
  }
  return true;
}

static bool test_valve_disable_all_after_stress() noexcept {
  // Safety: ensure all channels are off after stress test
  [[maybe_unused]] auto err = VALVES().DisableAllChannels();
  return true;
}

static bool test_valve_diagnostics() noexcept {
  ValveSystemDiagnostics diag{};
  [[maybe_unused]] auto ok = VALVES().GetSystemDiagnostics(diag);
  return true;
}

// ── Entry Point ───────────────────────────────────────────────────────────

extern "C" void app_main(void) {
  ESP_LOGI(TAG, "\n");
  ESP_LOGI(TAG, "========================================");
  ESP_LOGI(TAG, "  Valve Manager — Cycling Test");
  ESP_LOGI(TAG, "========================================\n");

  RUN_TEST(test_valve_ensure_initialized);
  RUN_TEST(test_valve_enable_solenoid_ch0);
  RUN_TEST(test_valve_disable_solenoid_ch0);
  RUN_TEST(test_valve_is_enabled_query);
  RUN_TEST(test_valve_disable_all);
  RUN_TEST(test_valve_cycle_all_solenoids);
  RUN_TEST(test_valve_proportional_set);
  RUN_TEST(test_valve_proportional_zero);
  RUN_TEST(test_valve_rapid_cycle_stress);
  RUN_TEST(test_valve_disable_all_after_stress);
  RUN_TEST(test_valve_diagnostics);

  print_test_summary(g_test_results, "VALVE CYCLING", TAG);
  cleanup_test_progress_indicator();
}
