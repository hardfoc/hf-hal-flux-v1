/**
 * @file FluxVersionManifest.cpp
 * @brief Static manifest of every component the Flux HAL links against.
 *
 * Driver semvers are picked up via `__has_include` so this compiles
 * cleanly even when a driver's generated header is not on the include
 * path (in which case the version column reads "n/a"). Git SHAs come
 * from the configure-time `flux_hal_git_manifest.h`.
 */

#include "FluxVersionManifest.h"
#include "FluxVersion.h"
#include "flux_hal_git_manifest.h"

// ── Driver semver headers (each defines HF_<X>_VERSION_STRING). ──────
#if __has_include("ads7952_version.h")
#  include "ads7952_version.h"
#  define _HF_VS_ADS7952 HF_ADS7952_VERSION_STRING
#else
#  define _HF_VS_ADS7952 "n/a"
#endif

#if __has_include("alicat_basis2_version.h")
#  include "alicat_basis2_version.h"
#  define _HF_VS_ALICAT_BASIS2 HF_ALICAT_BASIS2_VERSION_STRING
#else
#  define _HF_VS_ALICAT_BASIS2 "n/a"
#endif

#if __has_include("fdo2_version.h")
#  include "fdo2_version.h"
#  define _HF_VS_FDO2 HF_FDO2_VERSION_STRING
#else
#  define _HF_VS_FDO2 "n/a"
#endif

#if __has_include("max22200_version.h")
#  include "max22200_version.h"
#  define _HF_VS_MAX22200 HF_MAX22200_VERSION_STRING
#else
#  define _HF_VS_MAX22200 "n/a"
#endif

#if __has_include("mcp9700_version.h")
#  include "mcp9700_version.h"
#  define _HF_VS_MCP9700 HF_MCP9700_VERSION_STRING
#else
#  define _HF_VS_MCP9700 "n/a"
#endif

#if __has_include("ntc_thermistor_version.h")
#  include "ntc_thermistor_version.h"
#  define _HF_VS_NTC_THERMISTOR HF_NTC_THERMISTOR_VERSION_STRING
#else
#  define _HF_VS_NTC_THERMISTOR "n/a"
#endif

#if __has_include("tle92466ed_version.h")
#  include "tle92466ed_version.h"
#  define _HF_VS_TLE92466ED HF_TLE92466ED_VERSION_STRING
#else
#  define _HF_VS_TLE92466ED "n/a"
#endif

#if __has_include("ws2812_version.h")
#  include "ws2812_version.h"
#  define _HF_VS_WS2812_RMT HF_WS2812_RMT_VERSION_STRING
#else
#  define _HF_VS_WS2812_RMT "n/a"
#endif

#if __has_include("iiwrap_version.h")
#  include "iiwrap_version.h"
#  define _HF_VS_IIWRAP HF_IIWRAP_VERSION_STRING
#else
#  define _HF_VS_IIWRAP "n/a"
#endif

#if __has_include("pincfg_version.h")
#  include "pincfg_version.h"
#  define _HF_VS_PINCFG HF_PINCFG_VERSION_STRING
#else
#  define _HF_VS_PINCFG "n/a"
#endif

namespace flux_version {

namespace {

constexpr Entry kHalManifest[] = {
    // Top of the HAL stack.
    {"hf-hal-flux-v1",     FLUX_HAL_VERSION_STRING, HF_HAL_GIT_SHA_HAL},

    // Aggregator repos under the HAL.
    {"hf-core",            "n/a",                    HF_HAL_GIT_SHA_HF_CORE},
    {"hf-core-drivers",    "n/a",                    HF_HAL_GIT_SHA_HF_CORE_DRIVERS},
    {"hf-core-utils",      "n/a",                    HF_HAL_GIT_SHA_HF_CORE_UTILS},

    // External drivers compiled into this image.
    {"ads7952",            _HF_VS_ADS7952,           HF_HAL_GIT_SHA_ADS7952},
    {"alicat-basis2",      _HF_VS_ALICAT_BASIS2,     HF_HAL_GIT_SHA_ALICAT_BASIS2},
    {"fdo2",               _HF_VS_FDO2,              HF_HAL_GIT_SHA_FDO2},
    {"max22200",           _HF_VS_MAX22200,          HF_HAL_GIT_SHA_MAX22200},
    {"mcp9700",            _HF_VS_MCP9700,           HF_HAL_GIT_SHA_MCP9700},
    {"ntc-thermistor",     _HF_VS_NTC_THERMISTOR,    HF_HAL_GIT_SHA_NTC_THERMISTOR},
    {"tle92466ed",         _HF_VS_TLE92466ED,        HF_HAL_GIT_SHA_TLE92466ED},
    {"ws2812-rmt",         _HF_VS_WS2812_RMT,        HF_HAL_GIT_SHA_WS2812_RMT},

    // Internal driver-side submodules.
    {"hf-iiwrap",          _HF_VS_IIWRAP,            HF_HAL_GIT_SHA_IIWRAP},
    {"hf-pincfg",          _HF_VS_PINCFG,            HF_HAL_GIT_SHA_PINCFG},

    // Utility libs.
    {"hf-utils-canopen",   "n/a",                    HF_HAL_GIT_SHA_UTIL_CANOPEN},
    {"hf-utils-general",   "n/a",                    HF_HAL_GIT_SHA_UTIL_GENERAL},
    {"hf-utils-rtos-wrap", "n/a",                    HF_HAL_GIT_SHA_UTIL_RTOS},
};

constexpr std::size_t kHalManifestCount =
    sizeof(kHalManifest) / sizeof(kHalManifest[0]);

}  // namespace

EntrySpan GetHalManifest() noexcept {
    return EntrySpan{kHalManifest, kHalManifestCount};
}

const char* GetHalBuildStamp() noexcept {
    return HF_HAL_BUILD_STAMP;
}

}  // namespace flux_version
