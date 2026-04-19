/**
 * @file TestFramework.h
 * @brief Shared testing framework for ESP32 comprehensive test suites
 *
 * This file provides common testing infrastructure including test result tracking,
 * execution timing, and standardized test execution macros used across all
 * comprehensive test suites.
 *
 * @author Nebiyu Tadesse
 * @date 2025
 * @copyright HardFOC
 */

#pragma once

#include <memory>

#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"

#include "BaseThread.h"
#include "OsUtility.h"
#include "SignalSemaphore.h"

//=============================================================================
// GPIO14 TEST PROGRESSION INDICATOR MANAGEMENT
//=============================================================================

static bool g_test_progress_initialized = false;
static bool g_test_progress_state = false;
static constexpr gpio_num_t TEST_PROGRESS_PIN = GPIO_NUM_14;

inline bool init_test_progress_indicator() noexcept {
  if (g_test_progress_initialized) {
    return true;
  }

  gpio_config_t io_conf = {
      .pin_bit_mask = (1ULL << TEST_PROGRESS_PIN),
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };

  esp_err_t ret = gpio_config(&io_conf);
  if (ret != ESP_OK) {
    const char* TAG = "TestFramework";
    ESP_LOGE(TAG, "Failed to configure GPIO14: %s", esp_err_to_name(ret));
    return false;
  }

  gpio_set_level(TEST_PROGRESS_PIN, 0);
  g_test_progress_state = false;
  g_test_progress_initialized = true;

  return true;
}

inline void flip_test_progress_indicator() noexcept {
  if (!g_test_progress_initialized) {
    return;
  }

  g_test_progress_state = !g_test_progress_state;
  gpio_set_level(TEST_PROGRESS_PIN, g_test_progress_state ? 1 : 0);

  const char* TAG = "TestFramework";
  ESP_LOGI(TAG, "Test progression indicator: %s", g_test_progress_state ? "HIGH" : "LOW");

  os_delay_msec(50);
}

inline void cleanup_test_progress_indicator() noexcept {
  if (g_test_progress_initialized) {
    gpio_set_level(TEST_PROGRESS_PIN, 0);
    gpio_reset_pin(TEST_PROGRESS_PIN);
    g_test_progress_initialized = false;
    g_test_progress_state = false;
  }
}

inline void output_section_indicator(uint8_t blink_count = 5) noexcept {
  if (!g_test_progress_initialized) {
    return;
  }

  for (uint8_t i = 0; i < blink_count; ++i) {
    gpio_set_level(TEST_PROGRESS_PIN, 1);
    os_delay_msec(50);
    gpio_set_level(TEST_PROGRESS_PIN, 0);
    os_delay_msec(50);
  }

  g_test_progress_state = false;
}

inline void ensure_gpio14_initialized() noexcept {
  if (!g_test_progress_initialized) {
    init_test_progress_indicator();
  }
}

//=============================================================================
// TEST RESULTS TRACKING
//=============================================================================

struct TestResults {
  int total_tests = 0;
  int passed_tests = 0;
  int failed_tests = 0;
  uint64_t total_execution_time_us = 0;

  void add_result(bool passed, uint64_t execution_time) noexcept {
    total_tests++;
    total_execution_time_us += execution_time;
    if (passed) {
      passed_tests++;
    } else {
      failed_tests++;
    }
  }

  float get_success_percentage() const noexcept {
    return total_tests > 0 ? (static_cast<float>(passed_tests) / total_tests * 100.0f) : 0.0f;
  }

  float get_total_time_ms() const noexcept {
    return total_execution_time_us / 1000.0f;
  }
};

//=============================================================================
// TEST EXECUTION MACROS
//=============================================================================

#define RUN_TEST(test_func)                                                                        \
  do {                                                                                             \
    ensure_gpio14_initialized();                                                                   \
    ESP_LOGI(TAG,                                                                                  \
             "\n"                                                                                  \
             "╔══════════════════════════════════════════════════════════════════════════════╗\n"  \
             "║ Running: " #test_func "                                                     \n"    \
             "╚══════════════════════════════════════════════════════════════════════════════╝");  \
    uint64_t start_time = esp_timer_get_time();                                                    \
    bool result = test_func();                                                                     \
    uint64_t end_time = esp_timer_get_time();                                                      \
    uint64_t execution_time = end_time - start_time;                                               \
    g_test_results.add_result(result, execution_time);                                             \
    if (result) {                                                                                  \
      ESP_LOGI(TAG, "[SUCCESS] PASSED: " #test_func " (%.2f ms)", execution_time / 1000.0);        \
    } else {                                                                                       \
      ESP_LOGE(TAG, "[FAILED] FAILED: " #test_func " (%.2f ms)", execution_time / 1000.0);         \
    }                                                                                              \
    os_delay_msec(100);                                                                            \
  } while (0)

struct TestTaskContext {
  const char* test_name;
  bool (*test_func)() noexcept;
  TestResults* results;
  const char* tag;
  SignalSemaphore* completion_semaphore;
};

class TestTaskThread final : public BaseThread {
 public:
  static constexpr uint32_t kDefaultMinStackBytes = 4096;

  TestTaskThread(const char* thread_name,
                 TestTaskContext* context,
                 uint32_t requested_stack_bytes,
                 uint32_t priority) noexcept
      : BaseThread(thread_name),
        context_(context),
        stack_size_bytes_(requested_stack_bytes < kDefaultMinStackBytes ? kDefaultMinStackBytes
                                                                         : requested_stack_bytes),
        priority_(priority),
        executed_(false),
        stack_(nullptr) {}

 protected:
  bool Initialize() noexcept override {
    stack_ = std::make_unique<uint8_t[]>(stack_size_bytes_);
    if (!stack_) {
      return false;
    }

    return CreateBaseThread(stack_.get(),
                            stack_size_bytes_,
                            static_cast<OS_Uint>(priority_),
                            static_cast<OS_Uint>(priority_),
                            0,
                            OS_AUTO_START);
  }

  bool Setup() noexcept override {
    executed_ = false;
    return context_ != nullptr;
  }

  uint32_t Step() noexcept override {
    if (!context_ || executed_) {
      Stop();
      return 1;
    }

    executed_ = true;

    ESP_LOGI(context_->tag,
             "\n"
             "╔══════════════════════════════════════════════════════════════════════════════╗\n"
             "║ Running (task): %s                                                            \n"
             "╚══════════════════════════════════════════════════════════════════════════════╝",
             context_->test_name);

    const uint64_t start_time = esp_timer_get_time();
    const bool result = context_->test_func();
    const uint64_t end_time = esp_timer_get_time();
    const uint64_t execution_time = end_time - start_time;

    context_->results->add_result(result, execution_time);

    if (result) {
      ESP_LOGI(context_->tag, "[SUCCESS] PASSED (task): %s (%.2f ms)", context_->test_name,
               execution_time / 1000.0);
    } else {
      ESP_LOGE(context_->tag, "[FAILED] FAILED (task): %s (%.2f ms)", context_->test_name,
               execution_time / 1000.0);
    }

    if (context_->completion_semaphore != nullptr) {
      context_->completion_semaphore->Signal();
    }

    Stop();
    return 1;
  }

  bool Cleanup() noexcept override { return true; }

  bool ResetVariables() noexcept override {
    executed_ = false;
    return true;
  }

 private:
  TestTaskContext* context_;
  uint32_t stack_size_bytes_;
  uint32_t priority_;
  bool executed_;
  std::unique_ptr<uint8_t[]> stack_;
};

#define RUN_TEST_IN_TASK(name, func, stack_size_bytes, priority)                                   \
  do {                                                                                             \
    ensure_gpio14_initialized();                                                                   \
    TestTaskContext ctx;                                                                           \
    ctx.test_name = name;                                                                          \
    ctx.test_func = func;                                                                          \
    ctx.results = &g_test_results;                                                                 \
    ctx.tag = TAG;                                                                                 \
    SignalSemaphore completion_semaphore("TestTaskDone", name);                                    \
    ctx.completion_semaphore = &completion_semaphore;                                              \
    if (!completion_semaphore.EnsureInitialized()) {                                               \
      ESP_LOGE(TAG, "Failed to create completion semaphore for test: %s", name);                   \
      RUN_TEST(func);                                                                              \
    } else {                                                                                       \
      TestTaskThread test_thread(name, &ctx, static_cast<uint32_t>(stack_size_bytes),             \
                                 static_cast<uint32_t>(priority));                                 \
      if (!test_thread.EnsureInitialized()) {                                                      \
        ESP_LOGE(TAG, "Failed to initialize test thread: %s", name);                              \
        RUN_TEST(func);                                                                            \
      } else {                                                                                     \
        if (!test_thread.Start()) {                                                                \
          ESP_LOGE(TAG, "Failed to start test thread: %s", name);                                 \
          RUN_TEST(func);                                                                          \
        } else if (completion_semaphore.WaitUntilSignalled(30000)) {                              \
          ESP_LOGI(TAG, "Test task completed: %s", name);                                          \
        } else {                                                                                   \
          ESP_LOGW(TAG, "Test task timeout: %s", name);                                            \
        }                                                                                          \
        os_delay_msec(100);                                                                        \
      }                                                                                            \
    }                                                                                              \
  } while (0)

//=============================================================================
// TEST OUTPUT HELPERS
//=============================================================================

inline void print_test_summary(const TestResults& test_results, const char* test_suite_name,
                               const char* tag) noexcept {
  ensure_gpio14_initialized();
  ESP_LOGI(tag, "\n=== %s TEST SUMMARY ===", test_suite_name);
  ESP_LOGI(tag, "Total: %d, Passed: %d, Failed: %d, Success: %.2f%%, Time: %.2f ms",
           test_results.total_tests, test_results.passed_tests, test_results.failed_tests,
           test_results.get_success_percentage(), test_results.get_total_time_ms());

  if (test_results.failed_tests == 0) {
    ESP_LOGI(tag, "[SUCCESS] ALL %s TESTS PASSED!", test_suite_name);
  } else {
    ESP_LOGE(tag, "[FAILED] Some tests failed. Review the results above.");
  }
}

inline void print_test_section_header(const char* tag, const char* section_name,
                                      bool enabled = true) noexcept {
  if (enabled) {
    ESP_LOGI(tag, "\n");
    ESP_LOGI(tag, "╔══════════════════════════════════════════════════════════════════════════════╗");
    ESP_LOGI(tag, "║                              %s", section_name);
    ESP_LOGI(tag, "╠══════════════════════════════════════════════════════════════════════════════╣");
  } else {
    ESP_LOGI(tag, "\n");
    ESP_LOGI(tag, "╔══════════════════════════════════════════════════════════════════════════════╗");
    ESP_LOGI(tag, "║                         %s (DISABLED)", section_name);
    ESP_LOGI(tag, "╚══════════════════════════════════════════════════════════════════════════════╝");
  }
}

inline void print_test_section_footer(const char* tag, const char* section_name,
                                      bool enabled = true) noexcept {
  if (enabled) {
    ESP_LOGI(tag, "╚══════════════════════════════════════════════════════════════════════════════╝");
  }
}

#define RUN_TEST_SECTION_IF_ENABLED(define_name, section_name, ...)                                \
  do {                                                                                             \
    ensure_gpio14_initialized();                                                                   \
    if (define_name) {                                                                             \
      print_test_section_header(TAG, section_name, true);                                          \
      __VA_ARGS__                                                                                  \
      print_test_section_footer(TAG, section_name, true);                                          \
    } else {                                                                                       \
      print_test_section_header(TAG, section_name, false);                                         \
      ESP_LOGI(TAG, "Section disabled by configuration");                                          \
    }                                                                                              \
  } while (0)

#define RUN_SINGLE_TEST_IF_ENABLED(define_name, test_name, test_func, stack_size, priority)        \
  do {                                                                                             \
    ensure_gpio14_initialized();                                                                   \
    if (define_name) {                                                                             \
      RUN_TEST_IN_TASK(test_name, test_func, stack_size, priority);                                \
      flip_test_progress_indicator();                                                              \
    } else {                                                                                       \
      ESP_LOGI(TAG, "Test '%s' disabled by configuration", test_name);                             \
    }                                                                                              \
  } while (0)

#define RUN_TEST_GROUP_IF_ENABLED(define_name, section_name, ...)                                  \
  RUN_TEST_SECTION_IF_ENABLED(define_name, section_name, __VA_ARGS__)

#define RUN_TEST_SECTION_IF_ENABLED_WITH_PROGRESS(define_name, section_name, progress_func, ...)   \
  do {                                                                                             \
    ensure_gpio14_initialized();                                                                   \
    if (define_name) {                                                                             \
      print_test_section_header(TAG, section_name, true);                                          \
      __VA_ARGS__                                                                                  \
      if (progress_func)                                                                           \
        progress_func();                                                                           \
      print_test_section_footer(TAG, section_name, true);                                          \
    } else {                                                                                       \
      print_test_section_header(TAG, section_name, false);                                         \
      ESP_LOGI(TAG, "Section disabled by configuration");                                          \
    }                                                                                              \
  } while (0)

#define RUN_TEST_SECTION_IF_ENABLED_AUTO_PROGRESS(define_name, section_name, ...)                  \
  RUN_TEST_SECTION_IF_ENABLED_WITH_PROGRESS(define_name, section_name,                             \
                                            flip_test_progress_indicator, __VA_ARGS__)

#define RUN_TEST_SECTION_IF_ENABLED_WITH_PATTERN(define_name, section_name, blink_count, ...)      \
  do {                                                                                             \
    ensure_gpio14_initialized();                                                                   \
    if (define_name) {                                                                             \
      print_test_section_header(TAG, section_name, true);                                          \
      output_section_indicator(blink_count);                                                       \
      __VA_ARGS__                                                                                  \
      output_section_indicator(blink_count);                                                       \
      print_test_section_footer(TAG, section_name, true);                                          \
    } else {                                                                                       \
      print_test_section_header(TAG, section_name, false);                                         \
      ESP_LOGI(TAG, "Section disabled by configuration");                                          \
    }                                                                                              \
  } while (0)
