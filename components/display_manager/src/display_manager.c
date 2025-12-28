#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>
#include <driver/i2c.h>
#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_rgb.h>
#include <esp_lcd_touch.h>
#include <esp_lcd_touch_gt911.h>
#include <esp_log.h>
#include <esp_timer.h>

#include "display_manager.h"
#include "display_manager/page.h"
#include "lvgl.h"

static const char *TAG = "display_manager";

static const uint8_t k_io_expander_addr = 0x24;
static const uint8_t k_backlight_addr = 0x38;
static const uint8_t k_io_expander_output_cfg = 0x01;
static const uint8_t k_backlight_on = 0x1E;
static const uint8_t k_backlight_off = 0x1A;
static const uint8_t k_touch_reset_assert = 0x2C;
static const uint8_t k_touch_reset_release = 0x2E;
static const int k_i2c_timeout_ms = 1000;
static const int k_touch_reset_hold_ms = 100;
static const int k_touch_reset_release_ms = 200;

// Internal structure for display manager
struct display_manager {
    display_config_t config;
    esp_lcd_panel_handle_t panel_handle;
    esp_lcd_panel_io_handle_t touch_io_handle;
    esp_lcd_touch_handle_t touch_handle;
    lv_display_t *display;
    lv_timer_t *ui_timer;
    lv_indev_t *touch_indev;
    TaskHandle_t lvgl_task_handle;
    void *draw_buf1;
    void *draw_buf2;
    int i2c_port;

    // Page management
    dm_page_t **pages;
    int page_count;
    int current_page_index;
};

typedef struct {
    struct display_manager *dm;
    int page_index;
} display_manager_page_request_t;

// Forward declarations
static void display_manager_lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map);
static void display_manager_touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data);
static void display_manager_increase_lvgl_tick(void *arg);
static void display_manager_lvgl_port_task(void *arg);
static void display_manager_ui_timer_cb(lv_timer_t *t);
static void display_manager_apply_orientation(struct display_manager *dm);
static void display_manager_switch_to_page_internal(struct display_manager *dm, int page_index);
static void display_manager_switch_to_page_async_cb(void *user_data);

static void display_manager_delay_ms(uint32_t delay_ms)
{
    if (delay_ms == 0) {
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(delay_ms));
}

static esp_err_t display_manager_i2c_init(struct display_manager *dm)
{
    if (!dm) {
        return ESP_ERR_INVALID_ARG;
    }

    i2c_config_t i2c_conf = {};
    i2c_conf.mode = I2C_MODE_MASTER;
    i2c_conf.sda_io_num = dm->config.i2c_sda_io_num;
    i2c_conf.scl_io_num = dm->config.i2c_scl_io_num;
    i2c_conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    i2c_conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    i2c_conf.master.clk_speed = dm->config.i2c_freq_hz;

    esp_err_t err = i2c_param_config(dm->i2c_port, &i2c_conf);
    if (err != ESP_OK) {
        return err;
    }

    err = i2c_driver_install(dm->i2c_port, i2c_conf.mode, 0, 0, 0);
    if (err == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "I2C driver already installed");
        return ESP_OK;
    }

    return err;
}

static esp_err_t display_manager_i2c_write(struct display_manager *dm, uint8_t addr, uint8_t value)
{
    if (!dm) {
        return ESP_ERR_INVALID_ARG;
    }

    return i2c_master_write_to_device(dm->i2c_port, addr, &value, 1,
                                      pdMS_TO_TICKS(k_i2c_timeout_ms));
}

static esp_err_t display_manager_set_backlight(struct display_manager *dm, bool on)
{
    esp_err_t err = display_manager_i2c_write(dm, k_io_expander_addr, k_io_expander_output_cfg);
    if (err != ESP_OK) {
        return err;
    }

    uint8_t level = on ? k_backlight_on : k_backlight_off;
    return display_manager_i2c_write(dm, k_backlight_addr, level);
}

static esp_err_t display_manager_touch_reset(struct display_manager *dm)
{
    if (!dm) {
        return ESP_ERR_INVALID_ARG;
    }

    if (dm->config.touch_reset_io_num < 0) {
        return ESP_OK;
    }

    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = 1ULL << dm->config.touch_reset_io_num;
    gpio_config(&io_conf);

    esp_err_t err = display_manager_i2c_write(dm, k_io_expander_addr, k_io_expander_output_cfg);
    if (err != ESP_OK) {
        return err;
    }

    err = display_manager_i2c_write(dm, k_backlight_addr, k_touch_reset_assert);
    if (err != ESP_OK) {
        return err;
    }

    display_manager_delay_ms(k_touch_reset_hold_ms);
    gpio_set_level((gpio_num_t)dm->config.touch_reset_io_num, 0);
    display_manager_delay_ms(k_touch_reset_hold_ms);

    err = display_manager_i2c_write(dm, k_backlight_addr, k_touch_reset_release);
    if (err != ESP_OK) {
        return err;
    }

    display_manager_delay_ms(k_touch_reset_release_ms);
    return ESP_OK;
}

static esp_err_t display_manager_touch_init(struct display_manager *dm)
{
    if (!dm || !dm->config.touch_enabled) {
        return ESP_OK;
    }

    esp_err_t err = display_manager_touch_reset(dm);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Touch reset failed: %s", esp_err_to_name(err));
        return err;
    }

    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    const esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    err = esp_lcd_new_panel_io_i2c((esp_lcd_i2c_bus_handle_t)dm->i2c_port, &tp_io_config,
                                   &tp_io_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Touch IO init failed: %s", esp_err_to_name(err));
        return err;
    }

    bool swap_xy = false;
    bool mirror_x = false;
    bool mirror_y = false;
    if (dm->config.orientation == DISPLAY_ORIENTATION_PORTRAIT) {
        swap_xy = true;
        mirror_x = true;
    }

    esp_lcd_touch_config_t tp_cfg = {
        .x_max = dm->config.h_res,
        .y_max = dm->config.v_res,
        .rst_gpio_num = dm->config.touch_reset_io_num,
        .int_gpio_num = dm->config.touch_int_io_num,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = swap_xy,
            .mirror_x = mirror_x,
            .mirror_y = mirror_y,
        },
    };

    err = esp_lcd_touch_new_i2c_gt911(tp_io_handle, &tp_cfg, &dm->touch_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Touch controller init failed: %s", esp_err_to_name(err));
        esp_lcd_panel_io_del(tp_io_handle);
        return err;
    }

    dm->touch_io_handle = tp_io_handle;
    return ESP_OK;
}

display_manager_handle_t display_manager_init(const display_config_t *config)
{
    if (!config) {
        ESP_LOGE(TAG, "Display configuration is NULL");
        return NULL;
    }

    struct display_manager *dm = calloc(1, sizeof(struct display_manager));
    if (!dm) {
        ESP_LOGE(TAG, "Failed to allocate memory for display manager");
        return NULL;
    }

    memcpy(&dm->config, config, sizeof(display_config_t));
    if (dm->config.i2c_port < 0) {
        dm->config.i2c_port = 0;
    }
    if (dm->config.i2c_freq_hz <= 0) {
        dm->config.i2c_freq_hz = 400000;
    }
    if (dm->config.pixel_clock_hz <= 0) {
        dm->config.pixel_clock_hz = 16 * 1000 * 1000;
    }
    if (dm->config.data_width <= 0) {
        dm->config.data_width = 16;
    }
    if (dm->config.bits_per_pixel <= 0) {
        dm->config.bits_per_pixel = 16;
    }
    if (dm->config.num_fbs <= 0) {
        dm->config.num_fbs = 1;
    }
    if (dm->config.bounce_buffer_size_px < 0) {
        dm->config.bounce_buffer_size_px = 0;
    }
    if (dm->config.tick_period_ms <= 0) {
        dm->config.tick_period_ms = 2;
    }

    dm->i2c_port = dm->config.i2c_port;

    int effective_h_res = dm->config.h_res;
    int effective_v_res = dm->config.v_res;
    if (dm->config.orientation == DISPLAY_ORIENTATION_PORTRAIT) {
        effective_h_res = dm->config.v_res;
        effective_v_res = dm->config.h_res;
    }

    dm->current_page_index = -1;

    esp_err_t err = display_manager_i2c_init(dm);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init I2C: %s", esp_err_to_name(err));
        free(dm);
        return NULL;
    }

    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_rgb_panel_config_t panel_config = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings = {
            .pclk_hz = dm->config.pixel_clock_hz,
            .h_res = dm->config.h_res,
            .v_res = dm->config.v_res,
            .hsync_pulse_width = dm->config.hsync_pulse_width,
            .hsync_back_porch = dm->config.hsync_back_porch,
            .hsync_front_porch = dm->config.hsync_front_porch,
            .vsync_pulse_width = dm->config.vsync_pulse_width,
            .vsync_back_porch = dm->config.vsync_back_porch,
            .vsync_front_porch = dm->config.vsync_front_porch,
            .flags = {
                .pclk_active_neg = 1,
            },
        },
        .data_width = dm->config.data_width,
        .bits_per_pixel = dm->config.bits_per_pixel,
        .num_fbs = dm->config.num_fbs,
        .bounce_buffer_size_px = dm->config.bounce_buffer_size_px,
        .sram_trans_align = 4,
        .psram_trans_align = 64,
        .hsync_gpio_num = dm->config.hsync_io_num,
        .vsync_gpio_num = dm->config.vsync_io_num,
        .de_gpio_num = dm->config.de_io_num,
        .pclk_gpio_num = dm->config.pclk_io_num,
        .disp_gpio_num = dm->config.disp_io_num,
        .flags = {
            .fb_in_psram = dm->config.fb_in_psram,
        },
    };

    memcpy(panel_config.data_gpio_nums, dm->config.data_io_nums, sizeof(panel_config.data_gpio_nums));

    err = esp_lcd_new_rgb_panel(&panel_config, &panel_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create RGB panel: %s", esp_err_to_name(err));
        i2c_driver_delete(dm->i2c_port);
        free(dm);
        return NULL;
    }

    dm->panel_handle = panel_handle;
    ESP_ERROR_CHECK(esp_lcd_panel_init(dm->panel_handle));
    display_manager_apply_orientation(dm);

    err = display_manager_set_backlight(dm, true);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Backlight enable failed: %s", esp_err_to_name(err));
    }

    err = display_manager_touch_init(dm);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Touch init failed: %s", esp_err_to_name(err));
    }

    lv_init();

    dm->display = lv_display_create(effective_h_res, effective_v_res);

    int draw_buf_lines = dm->config.draw_buf_lines > 0 ? dm->config.draw_buf_lines : 40;
    size_t draw_buffer_sz = (size_t)effective_h_res * draw_buf_lines * sizeof(lv_color_t);
    dm->draw_buf1 = heap_caps_malloc(draw_buffer_sz, MALLOC_CAP_DMA);
    assert(dm->draw_buf1);
    dm->draw_buf2 = heap_caps_malloc(draw_buffer_sz, MALLOC_CAP_DMA);
    assert(dm->draw_buf2);

    lv_display_set_buffers(dm->display, dm->draw_buf1, dm->draw_buf2, draw_buffer_sz,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_user_data(dm->display, dm);
    lv_display_set_color_format(dm->display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(dm->display, display_manager_lvgl_flush_cb);

    if (dm->touch_handle) {
        dm->touch_indev = lv_indev_create();
        lv_indev_set_type(dm->touch_indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(dm->touch_indev, display_manager_touch_read_cb);
        lv_indev_set_user_data(dm->touch_indev, dm->touch_handle);
        lv_indev_set_display(dm->touch_indev, dm->display);
    }

    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &display_manager_increase_lvgl_tick,
        .arg = dm,
        .name = "lvgl_tick"
    };
    esp_timer_handle_t lvgl_tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, dm->config.tick_period_ms * 1000));

    dm->ui_timer = lv_timer_create(display_manager_ui_timer_cb, 100, dm);

    // Note: LVGL task is NOT started here. Call display_manager_start() after
    // adding all pages to avoid race conditions between page creation and
    // LVGL's timer handler.

    return dm;
}

void display_manager_deinit(display_manager_handle_t dm_handle)
{
    if (!dm_handle) {
        return;
    }

    for (int i = 0; i < dm_handle->page_count; i++) {
        if (dm_handle->pages[i]) {
            if (dm_handle->pages[i]->on_destroy) {
                dm_handle->pages[i]->on_destroy(dm_handle->pages[i]);
            }
            free(dm_handle->pages[i]);
        }
    }
    free(dm_handle->pages);

    if (dm_handle->lvgl_task_handle) {
        vTaskDelete(dm_handle->lvgl_task_handle);
    }

    if (dm_handle->touch_indev) {
        lv_indev_delete(dm_handle->touch_indev);
    }

    if (dm_handle->touch_handle) {
        esp_lcd_touch_del(dm_handle->touch_handle);
    }

    if (dm_handle->touch_io_handle) {
        esp_lcd_panel_io_del(dm_handle->touch_io_handle);
    }

    if (dm_handle->panel_handle) {
        esp_lcd_panel_del(dm_handle->panel_handle);
    }

    if (dm_handle->draw_buf1) {
        heap_caps_free(dm_handle->draw_buf1);
    }

    if (dm_handle->draw_buf2) {
        heap_caps_free(dm_handle->draw_buf2);
    }

    if (dm_handle->i2c_port >= 0) {
        i2c_driver_delete(dm_handle->i2c_port);
    }

    free(dm_handle);
}

void display_manager_add_page(display_manager_handle_t dm_handle, dm_page_t *page)
{
    if (!dm_handle || !page) {
        return;
    }

    dm_page_t **new_pages = realloc(dm_handle->pages,
                                    (dm_handle->page_count + 1) * sizeof(dm_page_t *));
    if (!new_pages) {
        ESP_LOGE(TAG, "Failed to allocate memory for new page");
        return;
    }

    dm_handle->pages = new_pages;
    dm_handle->pages[dm_handle->page_count] = page;
    dm_handle->page_count++;

    if (dm_handle->display) {
        lv_obj_t *scr = lv_display_get_screen_active(dm_handle->display);
        if (page->on_create) {
            page->on_create(page, scr);
        }
        page->is_created = true;

        if (dm_handle->current_page_index == -1 && dm_handle->page_count == 1) {
            display_manager_switch_to_page(dm_handle, 0);
        }
    }
}

static void display_manager_switch_to_page_internal(struct display_manager *dm_handle, int page_index)
{
    if (!dm_handle || page_index < 0 || page_index >= dm_handle->page_count) {
        return;
    }

    if (dm_handle->current_page_index >= 0 &&
        dm_handle->current_page_index < dm_handle->page_count &&
        dm_handle->pages[dm_handle->current_page_index]->is_visible) {
        if (dm_handle->pages[dm_handle->current_page_index]->on_hide) {
            dm_handle->pages[dm_handle->current_page_index]->on_hide(
                dm_handle->pages[dm_handle->current_page_index]);
        }
        dm_handle->pages[dm_handle->current_page_index]->is_visible = false;
    }

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
    if (!dm_handle) {
        return;
    }

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
    if (!dm_handle) {
        return;
    }

    if (dm_handle->current_page_index >= 0 &&
        dm_handle->current_page_index < dm_handle->page_count &&
        dm_handle->pages[dm_handle->current_page_index]->on_update) {
        dm_handle->pages[dm_handle->current_page_index]->on_update(
            dm_handle->pages[dm_handle->current_page_index]);
    }
}

static void display_manager_lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    struct display_manager *dm = lv_display_get_user_data(disp);
    if (!dm) {
        lv_display_flush_ready(disp);
        return;
    }

    int offsetx1 = area->x1 + dm->config.x_offset;
    int offsetx2 = area->x2 + dm->config.x_offset;
    int offsety1 = area->y1 + dm->config.y_offset;
    int offsety2 = area->y2 + dm->config.y_offset;

    esp_lcd_panel_draw_bitmap(dm->panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1,
                              px_map);
    lv_display_flush_ready(disp);
}

static void display_manager_touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    esp_lcd_touch_handle_t tp = (esp_lcd_touch_handle_t)lv_indev_get_user_data(indev);
    if (!data) {
        return;
    }

    if (!tp) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    esp_lcd_touch_point_data_t points[1] = {};
    uint8_t touch_count = 0;

    data->state = LV_INDEV_STATE_RELEASED;

    if (esp_lcd_touch_read_data(tp) != ESP_OK) {
        return;
    }

    if (esp_lcd_touch_get_data(tp, points, &touch_count, 1) != ESP_OK) {
        return;
    }

    if (touch_count > 0) {
        data->point.x = points[0].x;
        data->point.y = points[0].y;
        data->state = LV_INDEV_STATE_PRESSED;
    }
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
    (void)arg;
    uint32_t task_delay_ms = 500;

    ESP_LOGI(TAG, "Starting LVGL task");

    while (1) {
        task_delay_ms = lv_timer_handler();

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
    if (!dm) {
        return;
    }
    display_manager_update(dm);
}

static void display_manager_apply_orientation(struct display_manager *dm)
{
    if (!dm || !dm->panel_handle) {
        return;
    }

    bool swap_xy = false;
    bool mirror_x = false;
    bool mirror_y = false;

    if (dm->config.orientation == DISPLAY_ORIENTATION_PORTRAIT) {
        swap_xy = true;
        mirror_x = true;
    }

    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(dm->panel_handle, swap_xy));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(dm->panel_handle, mirror_x, mirror_y));
}

bool display_manager_start(display_manager_handle_t dm_handle)
{
    if (!dm_handle) {
        return false;
    }

    if (dm_handle->lvgl_task_handle) {
        ESP_LOGW(TAG, "LVGL task already started");
        return true;
    }

    ESP_LOGI(TAG, "Starting LVGL task");

    BaseType_t task_created = xTaskCreate(
        display_manager_lvgl_port_task,
        "LVGL",
        8192,
        dm_handle,
        2,
        &dm_handle->lvgl_task_handle
    );

    if (task_created != pdTRUE) {
        ESP_LOGE(TAG, "Failed to create LVGL task");
        return false;
    }

    return true;
}
