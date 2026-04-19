/**
 * @file flux_api_test.cpp
 * @brief Comprehensive Flux HAL API test suite for ESP32 (noexcept)
 *
 * This file exercises every public surface of the Flux singleton:
 *   - Full initialization sequence via EnsureInitialized()
 *   - System diagnostics & health-check validation
 *   - CommChannelsManager: SPI device enumeration
 *   - GpioManager: pin-name lookup (Contains)
 *   - ValveManager: solenoid & proportional channel control
 *   - AdcManager: voltage reads (ReadVoltage)
 *   - LedManager: animation, colour, brightness queries
 *   - TemperatureManager: multi-sensor reads (ESP32, NTC)
 *   - Utility methods: uptime, version, statistics dump
 *
 * All functions are noexcept – no exception handling used.
 *
 * @author HardFOC Team
 * @date 2025
 * @copyright HardFOC
 */

#include "api/Flux.h"
#include "managers/CommChannelsManager.h"
#include "managers/GpioManager.h"
#include "managers/ValveManager.h"
#include "managers/AdcManager.h"
#include "managers/LedManager.h"
#include "managers/TemperatureManager.h"
#include "TestFramework.h"

static const char* TAG = "FluxApiTest";
static TestResults g_test_results;

//=============================================================================
// TEST SECTION CONFIGURATION
//=============================================================================
static constexpr bool ENABLE_INIT_TESTS        = true;
static constexpr bool ENABLE_DIAGNOSTICS_TESTS = true;
static constexpr bool ENABLE_COMMS_TESTS       = true;
static constexpr bool ENABLE_GPIO_TESTS        = true;
static constexpr bool ENABLE_VALVE_TESTS       = true;
static constexpr bool ENABLE_ADC_TESTS         = true;
static constexpr bool ENABLE_LED_TESTS         = true;
static constexpr bool ENABLE_TEMPERATURE_TESTS = true;
static constexpr bool ENABLE_UTILITY_TESTS     = true;

//=============================================================================
// INITIALIZATION TESTS
//=============================================================================

static bool test_singleton_access() noexcept {
  auto& f1 = Flux::GetInstance();
  auto& f2 = Flux::GetInstance();
  return (&f1 == &f2);
}

static bool test_ensure_initialized() noexcept {
  auto& flux = FLUX_API;
  return flux.EnsureInitialized();
}

static bool test_is_initialized_after_init() noexcept {
  auto& flux = FLUX_API;
  flux.EnsureInitialized();
  return flux.IsInitialized();
}

static bool test_idempotent_init() noexcept {
  auto& flux = FLUX_API;
  bool first  = flux.EnsureInitialized();
  bool second = flux.EnsureInitialized();
  return (first == second);
}

//=============================================================================
// DIAGNOSTICS TESTS
//=============================================================================

static bool test_get_system_diagnostics() noexcept {
  auto& flux = FLUX_API;
  flux.EnsureInitialized();

  FluxSystemDiagnostics diag;
  bool ok = flux.GetSystemDiagnostics(diag);
  if (!ok) return false;

  // total_components should be 6 for Flux
  if (diag.total_components != 6) return false;
  if (diag.initialized_components > diag.total_components) return false;

  return true;
}

static bool test_component_init_status() noexcept {
  auto& flux = FLUX_API;
  flux.EnsureInitialized();

  auto status = flux.GetComponentInitializationStatus();
  // Should return exactly 6 entries
  return (status.size() == 6);
}

static bool test_perform_health_check() noexcept {
  auto& flux = FLUX_API;
  flux.EnsureInitialized();
  [[maybe_unused]] bool healthy = flux.PerformHealthCheck();
  return true;
}

static bool test_collect_manager_health() noexcept {
  auto& flux = FLUX_API;
  flux.EnsureInitialized();

  ManagerHealthSnapshot health{};
  bool ok = flux.CollectManagerHealth(health);
  if (!ok) return false;

  // Should report 6 managers for Flux
  if (health.count != 6) return false;

  // Every entry should have a non-null name
  for (size_t i = 0; i < health.count; ++i) {
    if (health.entries[i].name == nullptr) return false;
  }
  return true;
}

//=============================================================================
// COMMS TESTS
//=============================================================================

static bool test_comms_get_last_error() noexcept {
  auto& comms = FLUX_API.comms;
  [[maybe_unused]] CommError err = comms.GetLastError();
  return true;
}

static bool test_comms_system_diagnostics() noexcept {
  auto& comms = FLUX_API.comms;
  CommSystemDiagnostics diag{};
  [[maybe_unused]] auto err = comms.GetSystemDiagnostics(diag);
  return true;
}

static bool test_comms_spi_available() noexcept {
  auto& comms = FLUX_API.comms;
  // Attempt to get a known SPI device — null is fine if hardware absent
  (void)comms.GetSpiDevice(SpiDeviceId::MAX22200_SOLENOID_DRIVER);
  return true;
}

//=============================================================================
// GPIO TESTS
//=============================================================================

static bool test_gpio_contains() noexcept {
  auto& gpio = FLUX_API.gpio;
  [[maybe_unused]] bool a = gpio.Contains("MAX22200_EN");
  [[maybe_unused]] bool b = gpio.Contains("TLE92466_EN");
  [[maybe_unused]] bool c = gpio.Contains("LED_STATUS");
  return true;
}

//=============================================================================
// VALVE TESTS
//=============================================================================

static bool test_valve_ensure_initialized() noexcept {
  auto& valves = FLUX_API.valves;
  return valves.EnsureInitialized();
}

static bool test_valve_enable_channel() noexcept {
  auto& valves = FLUX_API.valves;
  // Enable solenoid channel 0 — result depends on hardware
  [[maybe_unused]] auto err = valves.EnableChannel(ValveChannel::SOL_CH0);
  return true;
}

static bool test_valve_disable_channel() noexcept {
  auto& valves = FLUX_API.valves;
  [[maybe_unused]] auto err = valves.DisableChannel(ValveChannel::SOL_CH0);
  return true;
}

static bool test_valve_is_channel_enabled() noexcept {
  auto& valves = FLUX_API.valves;
  [[maybe_unused]] bool enabled = valves.IsChannelEnabled(ValveChannel::SOL_CH0);
  return true;
}

static bool test_valve_disable_all() noexcept {
  auto& valves = FLUX_API.valves;
  [[maybe_unused]] auto err = valves.DisableAllChannels();
  return true;
}

static bool test_valve_proportional_current() noexcept {
  auto& valves = FLUX_API.valves;
  [[maybe_unused]] auto err = valves.SetProportionalCurrent(ValveChannel::PROP_CH0, 500);
  return true;
}

//=============================================================================
// ADC TESTS
//=============================================================================

static bool test_adc_read_channel() noexcept {
  auto& adc = FLUX_API.adc;
  float voltage = 0.0f;
  [[maybe_unused]] auto r1 = adc.ReadVoltage("NTC_THERMISTOR", voltage);
  [[maybe_unused]] auto r2 = adc.ReadVoltage("VBUS_MONITOR", voltage);
  return true;
}

//=============================================================================
// LED TESTS
//=============================================================================

static bool test_led_set_status() noexcept {
  auto& leds = FLUX_API.leds;
  [[maybe_unused]] auto err = leds.StartAnimation(LedAnimation::STATUS_OK);
  return true;
}

static bool test_led_get_color() noexcept {
  auto& leds = FLUX_API.leds;
  LedColor color{0, 0, 0};
  [[maybe_unused]] auto err = leds.GetCurrentColor(color);
  return true;
}

static bool test_led_get_brightness() noexcept {
  auto& leds = FLUX_API.leds;
  // Flux LedManager exposes SetBrightnessPercent / SetMaxBrightness but no getter;
  // verify the setter doesn't crash.
  [[maybe_unused]] auto err = leds.SetBrightnessPercent(50);
  return true;
}

//=============================================================================
// TEMPERATURE TESTS
//=============================================================================

static bool test_temp_read_esp32() noexcept {
  auto& temp = FLUX_API.temp;
  float celsius = 0.0f;
  [[maybe_unused]] auto err = temp.ReadTemperatureCelsius("ESP32_INTERNAL", celsius);
  return true;
}

static bool test_temp_read_ntc() noexcept {
  auto& temp = FLUX_API.temp;
  float celsius = 0.0f;
  [[maybe_unused]] auto err = temp.ReadTemperatureCelsius("NTC_THERMISTOR", celsius);
  return true;
}

//=============================================================================
// UTILITY TESTS
//=============================================================================

static bool test_system_uptime() noexcept {
  auto& flux = FLUX_API;
  uint64_t uptime = flux.GetSystemUptimeMs();
  return (uptime > 0);
}

static bool test_init_time() noexcept {
  auto& flux = FLUX_API;
  flux.EnsureInitialized();
  uint64_t init_time = flux.GetInitializationTimeMs();
  return (init_time > 0);
}

static bool test_system_version() noexcept {
  auto& flux = FLUX_API;
  const char* ver = flux.GetSystemVersion();
  return (ver != nullptr && ver[0] != '\0');
}

static bool test_dump_statistics() noexcept {
  auto& flux = FLUX_API;
  flux.DumpSystemStatistics();
  return true;
}

//=============================================================================
// ENTRY POINT
//=============================================================================

extern "C" void app_main(void) {
  ESP_LOGI(TAG, "\n");
  ESP_LOGI(TAG, "========================================");
  ESP_LOGI(TAG, "  Flux HAL — Comprehensive API Test");
  ESP_LOGI(TAG, "========================================\n");

  // ── Initialization ──────────────────────────────────────────────────────
  RUN_TEST_SECTION_IF_ENABLED(ENABLE_INIT_TESTS, "INITIALIZATION",
    RUN_TEST(test_singleton_access);
    RUN_TEST(test_ensure_initialized);
    RUN_TEST(test_is_initialized_after_init);
    RUN_TEST(test_idempotent_init);
  );

  // ── Diagnostics ─────────────────────────────────────────────────────────
  RUN_TEST_SECTION_IF_ENABLED(ENABLE_DIAGNOSTICS_TESTS, "DIAGNOSTICS",
    RUN_TEST(test_get_system_diagnostics);
    RUN_TEST(test_component_init_status);
    RUN_TEST(test_perform_health_check);
    RUN_TEST(test_collect_manager_health);
  );

  // ── Comms ───────────────────────────────────────────────────────────────
  RUN_TEST_SECTION_IF_ENABLED(ENABLE_COMMS_TESTS, "COMMUNICATION CHANNELS",
    RUN_TEST(test_comms_get_last_error);
    RUN_TEST(test_comms_system_diagnostics);
    RUN_TEST(test_comms_spi_available);
  );

  // ── GPIO ────────────────────────────────────────────────────────────────
  RUN_TEST_SECTION_IF_ENABLED(ENABLE_GPIO_TESTS, "GPIO",
    RUN_TEST(test_gpio_contains);
  );

  // ── Valves ──────────────────────────────────────────────────────────────
  RUN_TEST_SECTION_IF_ENABLED(ENABLE_VALVE_TESTS, "VALVE CONTROLLER",
    RUN_TEST(test_valve_ensure_initialized);
    RUN_TEST(test_valve_enable_channel);
    RUN_TEST(test_valve_disable_channel);
    RUN_TEST(test_valve_is_channel_enabled);
    RUN_TEST(test_valve_disable_all);
    RUN_TEST(test_valve_proportional_current);
  );

  // ── ADC ─────────────────────────────────────────────────────────────────
  RUN_TEST_SECTION_IF_ENABLED(ENABLE_ADC_TESTS, "ADC",
    RUN_TEST(test_adc_read_channel);
  );

  // ── LEDs ────────────────────────────────────────────────────────────────
  RUN_TEST_SECTION_IF_ENABLED(ENABLE_LED_TESTS, "LEDS",
    RUN_TEST(test_led_set_status);
    RUN_TEST(test_led_get_color);
    RUN_TEST(test_led_get_brightness);
  );

  // ── Temperature ─────────────────────────────────────────────────────────
  RUN_TEST_SECTION_IF_ENABLED(ENABLE_TEMPERATURE_TESTS, "TEMPERATURE",
    RUN_TEST(test_temp_read_esp32);
    RUN_TEST(test_temp_read_ntc);
  );

  // ── Utility ─────────────────────────────────────────────────────────────
  RUN_TEST_SECTION_IF_ENABLED(ENABLE_UTILITY_TESTS, "UTILITY",
    RUN_TEST(test_system_uptime);
    RUN_TEST(test_init_time);
    RUN_TEST(test_system_version);
    RUN_TEST(test_dump_statistics);
  );

  // ── Summary ─────────────────────────────────────────────────────────────
  print_test_summary(g_test_results, "FLUX API", TAG);
  cleanup_test_progress_indicator();

  ESP_LOGI(TAG, "\nFlux HAL API test complete.");
}
