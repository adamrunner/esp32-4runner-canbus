/*
 * Wheel Speed Page - Diagnostic vs Broadcast comparison
 */

#pragma once

#include "display_manager/page.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create the wheel speed page
 * @return Page object or NULL on failure
 */
dm_page_t *wheel_speed_page_create(void);

#ifdef __cplusplus
}
#endif
