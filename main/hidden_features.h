/*
 * Hidden features - pages kept out of the normal rotation.
 *
 * Holding a page title adds them for the current boot only; nothing is persisted.
 */

#pragma once

#include <stdbool.h>

#include "lvgl.h"

/**
 * @brief Add the RTC page to the rotation without enabling other hidden pages
 * @return true if the page is in the rotation
 */
bool hidden_features_add_rtc_page(void);

/**
 * @brief Add all hidden pages and switch to the first one
 *
 * Must run on the LVGL task.
 */
void hidden_features_enable(void);

/**
 * @brief Make a page title enable hidden features when held
 * @param title_label Title label object
 */
void hidden_features_attach_title(lv_obj_t *title_label);
