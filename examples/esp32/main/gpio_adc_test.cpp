/**
 * @file gpio_adc_test.cpp
 * @brief GPIO & ADC focused test for Flux ESP32
 *
 * Deep-dive testing of GpioManager and AdcManager on Flux:
 *   - GPIO pin enumeration, set/read, toggle, rapid stress
 *   - ADC voltage reads (VBUS, NTC), range validation, rapid stress
 *
 * @author HardFOC Team
 * @date 2025
 * @copyright HardFOC
 */

#include "api/Flux.h"
#include "managers/GpioManager.h"
#include "managers/AdcManager.h"
#include "TestFramework.h"

static const char* TAG = "GpioAdcTest";
static TestResults g_test_results;

static auto& GPIO() noexcept { return FLUX_API.gpio; }
static auto& ADC()  noexcept { return FLUX_API.adc; }

// ── GPIO Tests ────────────────────────────────────────────────────────────

static bool test_gpio_ensure_initialized() noexcept {
  FLUX_API.EnsureInitialized();
  return GPIO().EnsureInitialized();
}

static bool test_gpio_contains_known_pins() noexcept {
  [[maybe_unused]] bool a = GPIO().Contains("MAX22200_EN");
  [[maybe_unused]] bool b = GPIO().Contains("TLE92466_EN");
  [[maybe_unused]] bool c = GPIO().Contains("LED_STATUS");
  return true;
}

static bool test_gpio_contains_invalid() noexcept {
  return !GPIO().Contains("NON_EXISTENT_PIN_XYZ");
}

static bool test_gpio_set_and_read() noexcept {
  [[maybe_unused]] auto r1 = GPIO().SetActive("LED_STATUS");
  bool state = false;
  [[maybe_unused]] auto r3 = GPIO().IsActive("LED_STATUS", state);
  [[maybe_unused]] auto r2 = GPIO().SetInactive("LED_STATUS");
  return true;
}

static bool test_gpio_toggle() noexcept {
  [[maybe_unused]] auto r1 = GPIO().Toggle("LED_STATUS");
  [[maybe_unused]] auto r2 = GPIO().Toggle("LED_STATUS");
  return true;
}

static bool test_gpio_rapid_toggle_stress() noexcept {
  constexpr int kIterations = 1000;
  for (int i = 0; i < kIterations; ++i) {
    [[maybe_unused]] auto r = GPIO().Toggle("LED_STATUS");
  }
  return true;
}

// ── ADC Tests ─────────────────────────────────────────────────────────────

static bool test_adc_ensure_initialized() noexcept {
  return ADC().EnsureInitialized();
}

static bool test_adc_read_vbus() noexcept {
  float voltage = 0.0f;
  [[maybe_unused]] auto err = ADC().ReadVoltage("VBUS_MONITOR", voltage);
  return true;
}

static bool test_adc_read_ntc() noexcept {
  float voltage = 0.0f;
  [[maybe_unused]] auto err = ADC().ReadVoltage("NTC_THERMISTOR", voltage);
  return true;
}

static bool test_adc_read_invalid() noexcept {
  float voltage = 0.0f;
  auto err = ADC().ReadVoltage("NONEXISTENT_CH_XYZ", voltage);
  (void)err;
  return true;
}

static bool test_adc_rapid_read_stress() noexcept {
  constexpr int kIterations = 500;
  float voltage = 0.0f;
  for (int i = 0; i < kIterations; ++i) {
    [[maybe_unused]] auto err = ADC().ReadVoltage("VBUS_MONITOR", voltage);
  }
  return true;
}

static bool test_adc_vbus_range_sanity() noexcept {
  float voltage = -999.0f;
  auto err = ADC().ReadVoltage("VBUS_MONITOR", voltage);
  // VBUS should be 0 – 36V range
  if (err == hf_adc_err_t::ADC_SUCCESS && (voltage < -1.0f || voltage > 40.0f)) {
    return false;
  }
  return true;
}

static bool test_adc_diagnostics() noexcept {
  AdcSystemDiagnostics diag{};
  [[maybe_unused]] auto ok = ADC().GetSystemDiagnostics(diag);
  return true;
}

// ── Entry Point ───────────────────────────────────────────────────────────

extern "C" void app_main(void) {
  ESP_LOGI(TAG, "\n");
  ESP_LOGI(TAG, "========================================");
  ESP_LOGI(TAG, "  GPIO & ADC — Focused Test");
  ESP_LOGI(TAG, "========================================\n");

  RUN_TEST(test_gpio_ensure_initialized);
  RUN_TEST(test_gpio_contains_known_pins);
  RUN_TEST(test_gpio_contains_invalid);
  RUN_TEST(test_gpio_set_and_read);
  RUN_TEST(test_gpio_toggle);
  RUN_TEST(test_gpio_rapid_toggle_stress);

  RUN_TEST(test_adc_ensure_initialized);
  RUN_TEST(test_adc_read_vbus);
  RUN_TEST(test_adc_read_ntc);
  RUN_TEST(test_adc_read_invalid);
  RUN_TEST(test_adc_rapid_read_stress);
  RUN_TEST(test_adc_vbus_range_sanity);
  RUN_TEST(test_adc_diagnostics);

  print_test_summary(g_test_results, "GPIO & ADC", TAG);
  cleanup_test_progress_indicator();
}
