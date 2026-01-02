/*
 * 4Runner Data Page - Toyota-specific PIDs display
 */

#pragma once

#include "display_manager/page.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create the 4Runner data page
 * @return Page object or NULL on failure
 */
dm_page_t *fourrunner_page_create(void);

#ifdef __cplusplus
}
#endif
