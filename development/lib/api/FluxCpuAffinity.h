/**
 * @file FluxCpuAffinity.h
 * @brief Logical CPU indices for FreeRTOS task affinity on the Flux board.
 *
 * @details
 * Application and middleware pass these integers to `BaseThread` /
 * `os_thread_create_pinned` (via hf-core `OsAbstraction`). They are **HAL
 * board integration policy** for the current target, not portable silicon
 * constants: the mapping from logical index to vendor core names is
 * documented in the Flux V1 board / integration handbook (e.g. on
 * dual-core ESP32-S3, `kFluxCpuHostIo` is core 0 and `kFluxCpuRealtime` is
 * core 1, consistent with Espressif IDF “PRO_CPU” / “APP_CPU” terminology).
 *
 * On single-core builds, the RTOS wrapper ignores affinity; passing any
 * value here remains safe.
 *
 * @ingroup flux_api
 */

#pragma once

namespace flux_hal {

/// No CPU affinity (`tskNO_AFFINITY` / scheduler chooses).
inline constexpr int kFluxCpuNoAffinity = -1;

/// Logical core for host / system / comms style workloads (isolated from
/// high-rate control when the SoC is dual-core).
inline constexpr int kFluxCpuHostIo = 0;

/// Logical core for high-rate real-time loops (pressure, manifold, safety).
inline constexpr int kFluxCpuRealtime = 1;

}  // namespace flux_hal
