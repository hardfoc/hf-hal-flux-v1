#pragma once

#include "BaseThread.h"
#include "PressureSensor.h"
#include "PressureSensorConfig.h"
#include "handlers/ads7952/Ads7952Handler.h"

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

    /**
     * @param sample_interval_ms   Worker step period (ms). 1 ms (= 1 kHz)
     *                             matches the SSC sensor's internal ASIC rate.
     * @param avg_samples          ADC averaging used only by Sample() during
     *                             calibration (live path uses Auto-1 batch +
     *                             software IIR via PressureSensor::Update).
     * @param filter_cutoff_hz     IIR low-pass cutoff applied per channel.
     *                             0 disables filtering. Default 100 Hz at
     *                             1 kHz sampling gives ~1.6 ms group delay
     *                             — comfortable for control loops up to a
     *                             few hundred Hz.
     */
    PressureSensorThread(PressureSensorConfig* sensor_configs,
                         std::size_t num_sensors,
                         const CalibrationConfig& cal_config,
                         SharedState shared_state,
                         uint32_t sample_interval_ms,
                         uint8_t avg_samples,
                         float filter_cutoff_hz = 100.0f) noexcept;

    /// Reconfigure the per-channel low-pass filter cutoff at runtime.
    void SetFilterCutoff(float cutoff_hz) noexcept;

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
    float filter_cutoff_hz_;

    std::array<std::unique_ptr<PressureSensor>, kMaxSensors> sensors_;
    std::array<PressureSensor*, kMaxSensors> sensor_ptrs_;
    std::unique_ptr<PressureSensorCalibrator> calibrator_;

    // Batched live-sampling state ─ resolved once in Setup()
    Ads7952Handler* ads7952_handler_{nullptr};
    std::array<hf_channel_id_t, kMaxSensors> hw_channels_{};
    std::array<float,           kMaxSensors> batch_voltages_{};
    std::array<uint32_t,        kMaxSensors> batch_counts_{};

    uint32_t iteration_;
    uint8_t thread_stack_[kThreadStackBytes];
};
