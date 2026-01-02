/*
 * RTC Settings Page - Real-time clock configuration
 */

#pragma once

#include "display_manager/page.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create the RTC settings page
 * @return Page object or NULL on failure
 */
dm_page_t *rtc_page_create(void);

#ifdef __cplusplus
}
#endif
