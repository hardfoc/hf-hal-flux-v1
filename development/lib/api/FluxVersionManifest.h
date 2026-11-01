/**
 * @file FluxVersionManifest.h
 * @brief HAL-side inventory of every component the Flux HAL pulls in.
 *
 * The HAL knows which drivers and utility libraries it links against;
 * higher layers (middleware / application) intentionally do not look
 * across that boundary. This header exposes a stable read-only view
 * that consumers can iterate without learning the HAL's internal
 * submodule layout:
 *
 *   for (const auto& e : flux_version::GetHalManifest()) {
 *       printf("%s %s (%s)\n", e.name, e.version, e.git_sha);
 *   }
 *
 * Entries cover:
 *   - the HAL itself ("hf-hal-flux-v1")
 *   - hf-core / hf-core-drivers / hf-core-utils aggregator repos
 *   - every external driver actually compiled into this image
 *     (per the `HF_CORE_ENABLE_*` feature flags)
 *   - internal helpers (hf-internal-interface-wrap, hf-pincfg)
 *   - utility libs (hf-utils-canopen / -general / -rtos-wrap)
 *
 * Version strings come from each component's `<x>_version.h` (which
 * defines `HF_<X>_VERSION_STRING`). Components without a version
 * pattern report "n/a". Git short-SHAs come from
 * `flux_hal_git_manifest.h`, written at HAL configure time against
 * the actual checked-out HEAD of each submodule (so the SHA reflects
 * what landed in the firmware image, not a stale parent pointer).
 *
 * Add-on consumer pattern
 * -----------------------
 * When external board code uses a HAL handler base class
 * (`CommManager` / `GpioManager` / `AdcManager`) plus its own
 * off-board driver, that handler is the right place to publish its
 * driver's row. The HAL never touches it; the consumer concatenates
 * its own entries on top of `GetHalManifest()` for display.
 *
 * @author HardFOC Team
 * @date 2026
 * @version 1.0
 */

#ifndef FLUX_VERSION_MANIFEST_H_
#define FLUX_VERSION_MANIFEST_H_

#include <cstddef>

namespace flux_version {

/// One row in the HAL manifest. Pointers are static-storage and
/// remain valid for the program's lifetime.
struct Entry {
    const char* name;     ///< Component identifier (e.g. "fdo2", "hf-iiwrap").
    const char* version;  ///< "x.y.z" or "n/a" when no version pattern.
    const char* git_sha;  ///< Short SHA captured at HAL configure time.
};

/// Read-only view over a contiguous run of `Entry` records.
struct EntrySpan {
    const Entry* data;
    std::size_t  size;

    constexpr const Entry* begin() const noexcept { return data; }
    constexpr const Entry* end()   const noexcept { return data + size; }
};

/**
 * @brief Inventory of every component the HAL itself links against.
 * @return Span over the static manifest table (never empty).
 *
 * Stable across calls; safe to call from any task at any time
 * (no locking, no allocation).
 */
EntrySpan GetHalManifest() noexcept;

/**
 * @brief Build timestamp captured by the HAL at CMake configure time.
 * @return UTC timestamp string ("YYYY-MM-DD HH:MM:SS UTC").
 */
const char* GetHalBuildStamp() noexcept;

}  // namespace flux_version

#endif  // FLUX_VERSION_MANIFEST_H_
