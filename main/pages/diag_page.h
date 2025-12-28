/*
 * Diagnostics Page - OBD-II live metrics display
 */

#pragma once

#include "display_manager/page.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create the diagnostics page
 * @return Page object or NULL on failure
 */
dm_page_t *diag_page_create(void);

#ifdef __cplusplus
}
#endif
