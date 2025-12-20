/*
 * Page abstraction for display manager
 */

#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Forward declaration
typedef struct display_manager display_manager_t;

// Page structure
typedef struct dm_page {
    const char *name;
    void *user_data;
    
    // Page lifecycle callbacks
    void (*on_create)(struct dm_page *page, lv_obj_t *parent);
    void (*on_destroy)(struct dm_page *page);
    void (*on_show)(struct dm_page *page);
    void (*on_hide)(struct dm_page *page);
    void (*on_update)(struct dm_page *page);
    
    // Internal data
    lv_obj_t *container;
    bool is_created;
    bool is_visible;
} dm_page_t;

/**
 * @brief Create a new page
 *
 * @param name Name of the page
 * @param on_create Callback when page is created
 * @param on_destroy Callback when page is destroyed
 * @param on_show Callback when page is shown
 * @param on_hide Callback when page is hidden
 * @param on_update Callback for periodic updates
 * @return dm_page_t* Pointer to the new page
 */
dm_page_t *page_create(const char *name,
                       void (*on_create)(dm_page_t *page, lv_obj_t *parent),
                       void (*on_destroy)(dm_page_t *page),
                       void (*on_show)(dm_page_t *page),
                       void (*on_hide)(dm_page_t *page),
                       void (*on_update)(dm_page_t *page));

/**
 * @brief Destroy a page
 *
 * @param page Page to destroy
 */
void page_destroy(dm_page_t *page);

/**
 * @brief Show a page
 *
 * @param page Page to show
 */
void page_show(dm_page_t *page);

/**
 * @brief Hide a page
 *
 * @param page Page to hide
 */
void page_hide(dm_page_t *page);

/**
 * @brief Update a page
 *
 * @param page Page to update
 */
void page_update(dm_page_t *page);

#ifdef __cplusplus
}
#endif