/**
 * @file FluxVersion.h
 * @brief Version information for the HardFOC Flux V1 HAL.
 *
 * @author HardFOC Team
 * @date 2026
 * @version 1.1.0
 */

#ifndef FLUX_VERSION_H_
#define FLUX_VERSION_H_

// Version information
#define FLUX_HAL_VERSION_MAJOR    1   /**< @brief Major version number. */
#define FLUX_HAL_VERSION_MINOR    1   /**< @brief Minor version number. */
#define FLUX_HAL_VERSION_PATCH    0   /**< @brief Patch version number. */
#define FLUX_HAL_VERSION_STRING   "1.1.0"          /**< @brief Semantic version string. */

// Board information
#define FLUX_HAL_BOARD_NAME       "HardFOC Flux V1" /**< @brief Human-readable board name. */
#define FLUX_HAL_MCU              "ESP32-S3"        /**< @brief Target MCU identifier. */

// Build information
#define FLUX_HAL_BUILD_DATE       __DATE__ /**< @brief Compilation date stamp. */
#define FLUX_HAL_BUILD_TIME       __TIME__ /**< @brief Compilation time stamp. */

// Feature flags
#define FLUX_HAL_FEATURE_DIAGNOSTICS   1  /**< @brief Diagnostics subsystem enabled. */
#define FLUX_HAL_FEATURE_HEALTH_CHECK  1  /**< @brief Health-check subsystem enabled. */
#define FLUX_HAL_FEATURE_STATISTICS    1  /**< @brief Statistics/dump subsystem enabled. */

#endif // FLUX_VERSION_H_
