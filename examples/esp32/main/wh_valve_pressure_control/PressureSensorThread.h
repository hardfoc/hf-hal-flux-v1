#pragma once

#include "BaseThread.h"
#include "PressureSensor.h"
#include "PressureSensorConfig.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

class PressureSensorThread : public BaseThread {
public:
    struct SharedState {
        std::atomic<bool>& system_running;
        std::atomic<bool>& thread_ready;
        PressureSensor** sensor_slots;
        float& calibrated_va;
        bool& calibration_complete;
    };

    PressureSensorThread(PressureSensorConfig* sensor_configs,
                         std::size_t num_sensors,
                         const CalibrationConfig& cal_config,
                         SharedState shared_state,
                         uint32_t sample_interval_ms,
                         uint8_t avg_samples) noexcept;

protected:
    bool Initialize() noexcept override;
    bool Setup() noexcept override;
    uint32_t Step() noexcept override;
    bool Cleanup() noexcept override;
    bool ResetVariables() noexcept override;

private:
    static constexpr std::size_t kMaxSensors = 16;
    static constexpr std::size_t kThreadStackBytes = 8192;
    static constexpr OS_Uint kThreadPriority = 6;

    PressureSensorConfig* sensor_configs_;
    std::size_t num_sensors_;
    const CalibrationConfig& cal_config_;
    SharedState shared_state_;
    uint32_t sample_interval_ms_;
    uint8_t avg_samples_;

    std::array<std::unique_ptr<PressureSensor>, kMaxSensors> sensors_;
    std::array<PressureSensor*, kMaxSensors> sensor_ptrs_;
    std::unique_ptr<PressureSensorCalibrator> calibrator_;
    uint32_t iteration_;
    uint8_t thread_stack_[kThreadStackBytes];
};
