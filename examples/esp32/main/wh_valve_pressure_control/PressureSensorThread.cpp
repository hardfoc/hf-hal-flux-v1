#include "PressureSensorThread.h"

#include "OsUtility.h"
#include "esp_log.h"

#include <cstring>

static const char* TAG_PRESSURE = "Pressure";

PressureSensorThread::PressureSensorThread(PressureSensorConfig* sensor_configs,
                                           std::size_t num_sensors,
                                           const CalibrationConfig& cal_config,
                                           SharedState shared_state,
                                           uint32_t sample_interval_ms,
                                           uint8_t avg_samples) noexcept
    : BaseThread("PressureThread"),
      sensor_configs_(sensor_configs),
      num_sensors_(num_sensors),
      cal_config_(cal_config),
      shared_state_(shared_state),
      sample_interval_ms_(sample_interval_ms),
      avg_samples_(avg_samples),
      sensors_{},
      sensor_ptrs_{},
      calibrator_(nullptr),
      iteration_(0),
      thread_stack_{} {}

bool PressureSensorThread::Initialize() noexcept {
    return CreateBaseThread(thread_stack_,
                            sizeof(thread_stack_),
                            kThreadPriority,
                            kThreadPriority,
                            0,
                            OS_AUTO_START);
}

bool PressureSensorThread::Setup() noexcept {
    if (!sensor_configs_ || num_sensors_ == 0 || num_sensors_ > kMaxSensors) {
        ESP_LOGE(TAG_PRESSURE, "Invalid sensor configuration (count=%u)",
                 static_cast<unsigned>(num_sensors_));
        return false;
    }

    ESP_LOGI(TAG_PRESSURE, "Pressure thread setup");

    for (std::size_t i = 0; i < num_sensors_; ++i) {
        sensors_[i] = std::make_unique<PressureSensor>(sensor_configs_[i], cal_config_);
        if (!sensors_[i]) {
            ESP_LOGE(TAG_PRESSURE, "Failed to create PressureSensor CH%u", static_cast<unsigned>(i));
            return false;
        }
        sensor_ptrs_[i] = sensors_[i].get();
        shared_state_.sensor_slots[i] = sensors_[i].get();
    }

    calibrator_ = std::make_unique<PressureSensorCalibrator>(sensor_ptrs_.data(), num_sensors_, cal_config_);
    if (!calibrator_) {
        ESP_LOGE(TAG_PRESSURE, "Failed to create PressureSensorCalibrator");
        return false;
    }

    ESP_LOGI(TAG_PRESSURE, "Pressure sensor calibration start");
    os_delay_msec(2000);

    float va = calibrator_->CalibrateVA();
    if (va == 0.0f) {
        ESP_LOGE(TAG_PRESSURE, "VA calibration failed: %s", calibrator_->GetLastError());
        va = cal_config_.va_nominal;
        ESP_LOGW(TAG_PRESSURE, "Using nominal VA=%.2f V", static_cast<double>(va));
    }

    shared_state_.calibrated_va = va;
    calibrator_->ApplyVA(va);

    if (!calibrator_->CalibrateAllZeros()) {
        ESP_LOGW(TAG_PRESSURE, "Some sensors failed zero calibration");
    }

    for (std::size_t i = 0; i < num_sensors_; ++i) {
        for (int j = 0; j < 10; ++j) {
            sensors_[i]->Sample(avg_samples_);
            os_delay_msec(10);
        }
        os_delay_msec(20);
    }

    ESP_LOGI(TAG_PRESSURE, "Calibration complete: VA=%.3f V", static_cast<double>(shared_state_.calibrated_va));

    shared_state_.calibration_complete = true;
    shared_state_.thread_ready.store(true, std::memory_order_release);
    return true;
}

uint32_t PressureSensorThread::Step() noexcept {
    if (!shared_state_.system_running.load(std::memory_order_acquire)) {
        Stop();
        return 1;
    }

    for (std::size_t i = 0; i < num_sensors_; ++i) {
        sensors_[i]->Sample(avg_samples_);
    }

    const uint32_t status_period = (sample_interval_ms_ == 0)
                                       ? 200U
                                       : (2000U / sample_interval_ms_);

    if (status_period > 0 && (++iteration_ % status_period) == 0) {
        ESP_LOGI(TAG_PRESSURE, "─── Pressure Readings ───");
        for (std::size_t i = 0; i < num_sensors_; ++i) {
            const auto& sensor = *sensors_[i];
            const auto& cfg = sensor.GetConfig();
            if (sensor.IsOk()) {
                char flags[32] = "";
                if (sensor.IsSwapped()) {
                    strcat(flags, " [SW]");
                }

                ESP_LOGI(TAG_PRESSURE,
                         "  %-10s: %+8.4f %-4s  (V=%6.4f)%s",
                         cfg.label,
                         static_cast<double>(sensor.GetPressure()),
                         cfg.units,
                         static_cast<double>(sensor.GetVoltage()),
                         flags);
            } else {
                ESP_LOGW(TAG_PRESSURE,
                         "  %-10s: ERROR (count=%lu)",
                         cfg.label,
                         static_cast<unsigned long>(sensor.GetErrorCount()));
            }
        }
    }

    return sample_interval_ms_;
}

bool PressureSensorThread::Cleanup() noexcept {
    for (std::size_t i = 0; i < num_sensors_; ++i) {
        shared_state_.sensor_slots[i] = nullptr;
    }

    calibrator_.reset();
    for (std::size_t i = 0; i < num_sensors_; ++i) {
        sensors_[i].reset();
        sensor_ptrs_[i] = nullptr;
    }

    shared_state_.thread_ready.store(false, std::memory_order_release);
    return true;
}

bool PressureSensorThread::ResetVariables() noexcept {
    iteration_ = 0;
    shared_state_.calibration_complete = false;
    return true;
}
