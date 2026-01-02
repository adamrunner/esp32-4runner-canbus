/*
 * Settings Store - Persisted configuration values (NVS)
 */

#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Load CAN auto-start flag from NVS.
 * @param auto_start_out Output flag (false when unset or on failure).
 * @return true if the value was read or defaulted, false on NVS errors.
 */
bool settings_get_can_autostart(bool *auto_start_out);

/**
 * @brief Persist CAN auto-start flag to NVS.
 * @param enable true to auto-start CAN on boot.
 * @return true on success.
 */
bool settings_set_can_autostart(bool enable);

#ifdef __cplusplus
}
#endif
