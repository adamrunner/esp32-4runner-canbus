#include "display_manager/page.h"
#include <stdlib.h>
#include <string.h>

dm_page_t *page_create(const char *name,
                       void (*on_create)(dm_page_t *page, lv_obj_t *parent),
                       void (*on_destroy)(dm_page_t *page),
                       void (*on_show)(dm_page_t *page),
                       void (*on_hide)(dm_page_t *page),
                       void (*on_update)(dm_page_t *page))
{
    dm_page_t *page = calloc(1, sizeof(dm_page_t));
    if (!page) return NULL;
    
    page->name = name;
    page->on_create = on_create;
    page->on_destroy = on_destroy;
    page->on_show = on_show;
    page->on_hide = on_hide;
    page->on_update = on_update;
    
    return page;
}

void page_destroy(dm_page_t *page)
{
    if (!page) return;
    
    if (page->on_destroy) {
        page->on_destroy(page);
    }
    
    free(page);
}

void page_show(dm_page_t *page)
{
    if (!page || !page->is_created) return;
    
    if (page->on_show) {
        page->on_show(page);
    }
    page->is_visible = true;
}

void page_hide(dm_page_t *page)
{
    if (!page || !page->is_visible) return;
    
    if (page->on_hide) {
        page->on_hide(page);
    }
    page->is_visible = false;
}

void page_update(dm_page_t *page)
{
    if (!page || !page->is_visible) return;
    
    if (page->on_update) {
        page->on_update(page);
    }
}