/*
 * RPM Page - Diagnostic vs Broadcast RPM comparison
 */

#pragma once

#include "display_manager/page.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create the RPM test page
 * @return Page object or NULL on failure
 */
dm_page_t *rpm_page_create(void);

#ifdef __cplusplus
}
#endif
