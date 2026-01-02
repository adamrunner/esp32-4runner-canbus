/*
 * Orientation Page - Vehicle orientation and steering data
 */

#pragma once

#include "display_manager/page.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create the orientation page
 * @return Page object or NULL on failure
 */
dm_page_t *orientation_page_create(void);

#ifdef __cplusplus
}
#endif
