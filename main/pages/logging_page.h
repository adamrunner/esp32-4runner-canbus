/*
 * CAN Logging Page - SD card logging control
 */

#pragma once

#include "display_manager/page.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create the CAN logging page
 * @return Page object or NULL on failure
 */
dm_page_t *logging_page_create(void);

#ifdef __cplusplus
}
#endif
