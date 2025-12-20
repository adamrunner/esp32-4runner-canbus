#include <stdlib.h>
#include <string.h>
#include <driver/gpio.h>
#include <esp_lcd_panel_commands.h>
#include <esp_lcd_sh8601.h>
#include <esp_log.h>
#include <esp_timer.h>
#include "display_manager.h"
#include "display_manager/page.h"
#include "lvgl.h"
#include "draw/sw/lv_draw_sw.h"

static const char *TAG = "display_manager";

// Internal structure for display manager
struct display_manager {
    display_config_t config;
    esp_lcd_panel_io_handle_t io_handle;
    esp_lcd_panel_handle_t panel_handle;
    lv_display_t *display;
    lv_timer_t *lvgl_timer;
    lv_timer_t *ui_timer;

    // Page management
    dm_page_t **pages;
    int page_count;
    int current_page_index;

    // Task handles
    TaskHandle_t lvgl_task_handle;
};

typedef struct {
    struct display_manager *dm;
    int page_index;
} display_manager_page_request_t;

// Forward declarations
static void display_manager_lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map);
static void display_manager_increase_lvgl_tick(void *arg);
static void display_manager_lvgl_port_task(void *arg);
static void display_manager_ui_timer_cb(lv_timer_t *t);
static void display_manager_apply_orientation(struct display_manager *dm);
static void display_manager_switch_to_page_internal(struct display_manager *dm, int page_index);
static void display_manager_switch_to_page_async_cb(void *user_data);

display_manager_handle_t display_manager_init(const display_config_t *config)
{
    struct display_manager *dm = calloc(1, sizeof(struct display_manager));
    if (!dm) {
        ESP_LOGE(TAG, "Failed to allocate memory for display manager");
        return NULL;
    }

    // Copy configuration
    memcpy(&dm->config, config, sizeof(display_config_t));
    int effective_h_res = config->h_res;
    int effective_v_res = config->v_res;
    if (config->orientation == DISPLAY_ORIENTATION_LANDSCAPE) {
        effective_h_res = config->v_res;
        effective_v_res = config->h_res;
    }

    // Initialize page management
    dm->current_page_index = -1; // No page selected initially

    // Initialize GPIO for backlight (if provided)
    if (config->bk_light_io_num >= 0) {
        ESP_ERROR_CHECK(gpio_reset_pin(config->bk_light_io_num));
        gpio_config_t bk_gpio_config = {
            .mode = GPIO_MODE_OUTPUT,
            .pin_bit_mask = 1ULL << config->bk_light_io_num,
            .intr_type = GPIO_INTR_DISABLE,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE
        };
        ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));
        ESP_ERROR_CHECK(gpio_set_level(config->bk_light_io_num, config->bk_light_off_level));
    }

    // Initialize SPI bus
    spi_bus_config_t buscfg = {
        .sclk_io_num = config->sclk_io_num,
        .mosi_io_num = config->mosi_io_num,
        .miso_io_num = config->miso_io_num,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = effective_h_res * 80 * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(config->spi_host, &buscfg, SPI_DMA_CH_AUTO));

    // Install panel IO
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = config->dc_io_num,
        .cs_gpio_num = config->cs_io_num,
        .pclk_hz = config->pixel_clock_hz,
        .lcd_cmd_bits = config->cmd_bits,
        .lcd_param_bits = config->param_bits,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)config->spi_host, &io_config, &dm->io_handle));

    // SH8601 initialization commands (from working example)
    static const uint8_t init_cmd_b2[] = {0x0c,0x0c,0x00,0x33,0x33};
    static const uint8_t init_cmd_b7[] = {0x35};
    static const uint8_t init_cmd_bb[] = {0x13};
    static const uint8_t init_cmd_c0[] = {0x2c};
    static const uint8_t init_cmd_c2[] = {0x01};
    static const uint8_t init_cmd_c3[] = {0x0b};
    static const uint8_t init_cmd_c4[] = {0x20};
    static const uint8_t init_cmd_c6[] = {0x0f};
    static const uint8_t init_cmd_d0[] = {0xa4,0xa1};
    static const uint8_t init_cmd_d6[] = {0xa1};
    static const uint8_t init_cmd_e0[] = {0x00,0x03,0x07,0x08,0x07,0x15,0x2A,0x44,0x42,0x0A,0x17,0x18,0x25,0x27};
    static const uint8_t init_cmd_e1[] = {0x00,0x03,0x08,0x07,0x07,0x23,0x2A,0x43,0x42,0x09,0x18,0x17,0x25,0x27};
    static const uint8_t init_cmd_21[] = {0x21};
    static const uint8_t init_cmd_11[] = {0x11};
    static const uint8_t init_cmd_29[] = {0x29};

    static const sh8601_lcd_init_cmd_t lcd_init_cmds[] = {
        {0xb2, init_cmd_b2, 5, 0},
        {0xb7, init_cmd_b7, 1, 0},
        {0xbb, init_cmd_bb, 1, 0},
        {0xc0, init_cmd_c0, 1, 0},
        {0xc2, init_cmd_c2, 1, 0},
        {0xc3, init_cmd_c3, 1, 0},
        {0xc4, init_cmd_c4, 1, 0},
        {0xc6, init_cmd_c6, 1, 0},
        {0xd0, init_cmd_d0, 2, 0},
        {0xd6, init_cmd_d6, 1, 0},
        {0xe0, init_cmd_e0, 14, 0},
        {0xe1, init_cmd_e1, 14, 0},
        {0x21, init_cmd_21, 0, 0},
        {0x11, init_cmd_11, 0, 120},
        {0x29, init_cmd_29, 0, 0},
    };

    sh8601_vendor_config_t vendor_config = {
        .init_cmds = lcd_init_cmds,
        .init_cmds_size = sizeof(lcd_init_cmds) / sizeof(lcd_init_cmds[0]),
    };

    // Install panel driver
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = config->rst_io_num,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = &vendor_config,
        .data_endian = LCD_RGB_DATA_ENDIAN_BIG,
    };

    // Use SH8601 LCD driver
    ESP_ERROR_CHECK(esp_lcd_new_panel_sh8601(dm->io_handle, &panel_config, &dm->panel_handle));

    // Initialize panel
    ESP_ERROR_CHECK(esp_lcd_panel_reset(dm->panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(dm->panel_handle));
    display_manager_apply_orientation(dm);

    // Turn on backlight
    if (config->bk_light_io_num >= 0) {
        ESP_ERROR_CHECK(gpio_set_level(config->bk_light_io_num, config->bk_light_on_level));
    }

    // Initialize LVGL
    lv_init();

    // Create display (LVGL 9 API)
    dm->display = lv_display_create(effective_h_res, effective_v_res);

    // Allocate draw buffers
    size_t draw_buffer_sz = effective_h_res * config->draw_buf_lines * sizeof(lv_color16_t);
    void *buf1 = heap_caps_malloc(draw_buffer_sz, MALLOC_CAP_DMA);
    assert(buf1);
    void *buf2 = heap_caps_malloc(draw_buffer_sz, MALLOC_CAP_DMA);
    assert(buf2);

    // Set up display buffers and callbacks (LVGL 9 API)
    lv_display_set_buffers(dm->display, buf1, buf2, draw_buffer_sz, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_user_data(dm->display, dm);
    lv_display_set_color_format(dm->display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(dm->display, display_manager_lvgl_flush_cb);

    // Set up LVGL tick timer
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &display_manager_increase_lvgl_tick,
        .arg = dm,
        .name = "lvgl_tick"
    };
    esp_timer_handle_t lvgl_tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, config->tick_period_ms * 1000));

    // Note: Working examples don't use event callbacks

    // Schedule periodic UI updates on LVGL thread
    dm->ui_timer = lv_timer_create(display_manager_ui_timer_cb, 100, dm);

    // Create LVGL task (increased stack size for complex layouts)
    BaseType_t task_created = xTaskCreate(
        display_manager_lvgl_port_task,
        "LVGL",
        8192,
        dm,
        2,
        &dm->lvgl_task_handle
    );

    if (task_created != pdTRUE) {
        ESP_LOGE(TAG, "Failed to create LVGL task");
        free(dm);
        return NULL;
    }

    return dm;
}

void display_manager_deinit(display_manager_handle_t dm_handle)
{
    if (!dm_handle) return;

    // Free pages
    for (int i = 0; i < dm_handle->page_count; i++) {
        if (dm_handle->pages[i]) {
            if (dm_handle->pages[i]->on_destroy) {
                dm_handle->pages[i]->on_destroy(dm_handle->pages[i]);
            }
            free(dm_handle->pages[i]);
        }
    }
    free(dm_handle->pages);

    // Clean up LVGL
    if (dm_handle->lvgl_task_handle) {
        vTaskDelete(dm_handle->lvgl_task_handle);
    }

    // Deinitialize hardware
    esp_lcd_panel_del(dm_handle->panel_handle);
    esp_lcd_panel_io_del(dm_handle->io_handle);
    spi_bus_free(dm_handle->config.spi_host);

    free(dm_handle);
}

void display_manager_add_page(display_manager_handle_t dm_handle, dm_page_t *page)
{
    if (!dm_handle || !page) return;

    dm_page_t **new_pages = realloc(dm_handle->pages, (dm_handle->page_count + 1) * sizeof(dm_page_t*));
    if (!new_pages) {
        ESP_LOGE(TAG, "Failed to allocate memory for new page");
        return;
    }

    dm_handle->pages = new_pages;
    dm_handle->pages[dm_handle->page_count] = page;
    dm_handle->page_count++;

    // Create the page if we're already initialized
    if (dm_handle->display) {
        lv_obj_t *scr = lv_display_get_screen_active(dm_handle->display);
        if (page->on_create) {
            page->on_create(page, scr);
        }
        page->is_created = true;

        // Show the first page automatically if no page is currently selected
        if (dm_handle->current_page_index == -1 && dm_handle->page_count == 1) {
            display_manager_switch_to_page(dm_handle, 0);
        }
    }
}

static void display_manager_switch_to_page_internal(struct display_manager *dm_handle, int page_index)
{
    if (!dm_handle || page_index < 0 || page_index >= dm_handle->page_count) return;

    // Hide current page
    if (dm_handle->current_page_index >= 0 &&
        dm_handle->current_page_index < dm_handle->page_count &&
        dm_handle->pages[dm_handle->current_page_index]->is_visible) {
        if (dm_handle->pages[dm_handle->current_page_index]->on_hide) {
            dm_handle->pages[dm_handle->current_page_index]->on_hide(dm_handle->pages[dm_handle->current_page_index]);
        }
        dm_handle->pages[dm_handle->current_page_index]->is_visible = false;
    }

    // Show new page
    dm_handle->current_page_index = page_index;
    if (dm_handle->pages[page_index]->on_show) {
        dm_handle->pages[page_index]->on_show(dm_handle->pages[page_index]);
    }
    dm_handle->pages[page_index]->is_visible = true;
}

static void display_manager_switch_to_page_async_cb(void *user_data)
{
    display_manager_page_request_t *req = user_data;

    if (!req) {
        return;
    }

    display_manager_switch_to_page_internal(req->dm, req->page_index);
    free(req);
}

void display_manager_switch_to_page(display_manager_handle_t dm_handle, int page_index)
{
    if (!dm_handle) return;

    if (xTaskGetCurrentTaskHandle() == dm_handle->lvgl_task_handle) {
        display_manager_switch_to_page_internal(dm_handle, page_index);
        return;
    }

    if (page_index < 0 || page_index >= dm_handle->page_count) {
        ESP_LOGW(TAG, "Invalid page index %d", page_index);
        return;
    }

    display_manager_page_request_t *req = malloc(sizeof(*req));

    if (!req) {
        ESP_LOGE(TAG, "Failed to allocate page switch request");
        return;
    }

    req->dm = dm_handle;
    req->page_index = page_index;

    if (lv_async_call(display_manager_switch_to_page_async_cb, req) != LV_RESULT_OK) {
        ESP_LOGE(TAG, "Failed to schedule page switch");
        free(req);
    }
}

lv_display_t *display_manager_get_display(display_manager_handle_t dm_handle)
{
    return dm_handle ? dm_handle->display : NULL;
}

int display_manager_get_page_count(display_manager_handle_t dm_handle)
{
    return dm_handle ? dm_handle->page_count : 0;
}

void display_manager_update(display_manager_handle_t dm_handle)
{
    if (!dm_handle) return;

    // Update current page
    if (dm_handle->current_page_index >= 0 &&
        dm_handle->current_page_index < dm_handle->page_count &&
        dm_handle->pages[dm_handle->current_page_index]->on_update) {
        dm_handle->pages[dm_handle->current_page_index]->on_update(dm_handle->pages[dm_handle->current_page_index]);
    }
}

// LVGL callback implementations
static void display_manager_lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    struct display_manager *dm = lv_display_get_user_data(disp);
    if (!dm) {
        lv_display_flush_ready(disp);
        return;
    }

    esp_lcd_panel_handle_t panel_handle = dm->panel_handle;

    int offsetx1 = area->x1 + dm->config.x_offset;
    int offsetx2 = area->x2 + dm->config.x_offset;
    int offsety1 = area->y1 + dm->config.y_offset;
    int offsety2 = area->y2 + dm->config.y_offset;

    /* Swap RGB565 byte order for SH8601 panel */
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
    lv_draw_sw_rgb565_swap(px_map, w * h);

    esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, px_map);

    // Inform LVGL that you are ready with the flushing (LVGL 9 API)
    lv_display_flush_ready(disp);
}


static void display_manager_increase_lvgl_tick(void *arg)
{
    struct display_manager *dm = (struct display_manager *)arg;
    uint32_t tick_ms = 2;

    if (dm && dm->config.tick_period_ms > 0) {
        tick_ms = (uint32_t)dm->config.tick_period_ms;
    }

    lv_tick_inc(tick_ms);
}

static void display_manager_lvgl_port_task(void *arg)
{
    struct display_manager *dm = (struct display_manager *)arg;
    uint32_t task_delay_ms = 500; // Max delay like working example

    ESP_LOGI(TAG, "Starting LVGL task");

    while (1) {
        task_delay_ms = lv_timer_handler();

        // Constrain delay like working example
        if (task_delay_ms > 500) {
            task_delay_ms = 500;
        } else if (task_delay_ms < 1) {
            task_delay_ms = 1;
        }
        vTaskDelay(pdMS_TO_TICKS(task_delay_ms));
    }
}

static void display_manager_ui_timer_cb(lv_timer_t *t)
{
    struct display_manager *dm = (struct display_manager *)lv_timer_get_user_data(t);
    if (!dm) return;
    display_manager_update(dm);
}

static void display_manager_apply_orientation(struct display_manager *dm)
{
    uint8_t madctl = 0x00;
    if (dm->config.orientation == DISPLAY_ORIENTATION_LANDSCAPE) {
        madctl = 0x70;
    }
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(dm->io_handle, LCD_CMD_MADCTL, &madctl, 1));
}
