/**
 * @file HfValveErrorMapping.h
 * @ingroup valve
 * @brief Constexpr translation functions from driver-native error types to hf_valve_err_t.
 *
 * @details
 * Maps error codes from both valve driver ICs to the unified hf_valve_err_t enum:
 *   - TLE92466ED: tle92466ed::DriverError (15 codes) → hf_valve_err_t
 *   - MAX22200:   max22200::DriverStatus  (6 codes)  → hf_valve_err_t
 *
 * All functions are constexpr — zero runtime overhead for compile-time-known inputs.
 *
 * @author HardFOC Team
 * @date 2026
 * @copyright HardFOC
 */

#pragma once

#include "HfValveError.h"
#include "core/hf-core-drivers/external/hf-tle92466ed-driver/inc/tle92466ed.hpp"
#include "core/hf-core-drivers/external/hf-max22200-driver/inc/max22200_types.hpp"

/**
 * @ingroup valve
 * @brief Map TLE92466ED DriverError → hf_valve_err_t
 * @param e Driver error code from TLE92466ED operations
 * @return Corresponding unified valve error code
 */
constexpr hf_valve_err_t TleDriverErrorToValveError(tle92466ed::DriverError e) noexcept {
  switch (e) {
    case tle92466ed::DriverError::None:
      return hf_valve_err_t::VALVE_SUCCESS;
    case tle92466ed::DriverError::NotInitialized:
      return hf_valve_err_t::VALVE_ERR_NOT_INITIALIZED;
    case tle92466ed::DriverError::HardwareError:
      return hf_valve_err_t::VALVE_ERR_HARDWARE_FAULT;
    case tle92466ed::DriverError::InvalidChannel:
      return hf_valve_err_t::VALVE_ERR_INVALID_CHANNEL;
    case tle92466ed::DriverError::InvalidParameter:
      return hf_valve_err_t::VALVE_ERR_INVALID_PARAMETER;
    case tle92466ed::DriverError::DeviceNotResponding:
      return hf_valve_err_t::VALVE_ERR_DEVICE_NOT_RESPONDING;
    case tle92466ed::DriverError::WrongDeviceID:
      return hf_valve_err_t::VALVE_ERR_WRONG_DEVICE_ID;
    case tle92466ed::DriverError::RegisterError:
      return hf_valve_err_t::VALVE_ERR_REGISTER_ERROR;
    case tle92466ed::DriverError::CRCError:
      return hf_valve_err_t::VALVE_ERR_SPI_CRC_ERROR;
    case tle92466ed::DriverError::FaultDetected:
      return hf_valve_err_t::VALVE_ERR_HARDWARE_FAULT;
    case tle92466ed::DriverError::ConfigurationError:
      return hf_valve_err_t::VALVE_ERR_CONFIGURATION_ERROR;
    case tle92466ed::DriverError::TimeoutError:
      return hf_valve_err_t::VALVE_ERR_TIMEOUT;
    case tle92466ed::DriverError::WrongMode:
      return hf_valve_err_t::VALVE_ERR_WRONG_MODE;
    case tle92466ed::DriverError::SPIFrameError:
      return hf_valve_err_t::VALVE_ERR_SPI_FRAME_ERROR;
    case tle92466ed::DriverError::WriteToReadOnly:
      return hf_valve_err_t::VALVE_ERR_WRITE_TO_READ_ONLY;
    default:
      return hf_valve_err_t::VALVE_ERR_UNKNOWN;
  }
}

/**
 * @ingroup valve
 * @brief Map MAX22200 DriverStatus → hf_valve_err_t
 * @param s Driver status code from MAX22200 operations
 * @return Corresponding unified valve error code
 */
constexpr hf_valve_err_t MaxDriverStatusToValveError(max22200::DriverStatus s) noexcept {
  switch (s) {
    case max22200::DriverStatus::OK:
      return hf_valve_err_t::VALVE_SUCCESS;
    case max22200::DriverStatus::INITIALIZATION_ERROR:
      return hf_valve_err_t::VALVE_ERR_NOT_INITIALIZED;
    case max22200::DriverStatus::COMMUNICATION_ERROR:
      return hf_valve_err_t::VALVE_ERR_SPI_BUS_ERROR;
    case max22200::DriverStatus::INVALID_PARAMETER:
      return hf_valve_err_t::VALVE_ERR_INVALID_PARAMETER;
    case max22200::DriverStatus::HARDWARE_FAULT:
      return hf_valve_err_t::VALVE_ERR_HARDWARE_FAULT;
    case max22200::DriverStatus::TIMEOUT:
      return hf_valve_err_t::VALVE_ERR_TIMEOUT;
    default:
      return hf_valve_err_t::VALVE_ERR_UNKNOWN;
  }
}
