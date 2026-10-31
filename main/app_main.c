// freetarget-display: freETarget shot display for the CYD (ESP32-2432S028R).
//
// Joins the target's WiFi, reads shots over TCP and draws them on the 2.8"
// screen. BOOT button: tap = auto zoom / full face, hold 1 s = next face,
// hold 4 s = clear.
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"

#include "lcd.h"
#include "model.h"
#include "net.h"
#include "sd.h"
#include "ui.h"

static const char *TAG = "fet";

#define PIN_BOOT   0
#define STRIP_H    24

static SemaphoreHandle_t s_lock;
void model_lock(void)   { xSemaphoreTake(s_lock, portMAX_DELAY); }
void model_unlock(void) { xSemaphoreGive(s_lock); }

static model_t   s_snap;      // big; keep it off the task stack
static uint16_t *s_strip;

static void draw(int x, int y, int w, int h, bool blink)
{
    for (int yy = y; yy < y + h; yy += STRIP_H) {
        int sh = (y + h - yy) < STRIP_H ? (y + h - yy) : STRIP_H;
        ui_render(&s_snap, blink, x, yy, w, sh, s_strip);
        lcd_blit(x, yy, w, sh, s_strip);
    }
}

// --- Face choice, saved in NVS ----------------------------------------------
static const int FACE_MENU[] = FET_FACE_MENU;
#define FACE_MENU_N ((int)(sizeof(FACE_MENU) / sizeof(FACE_MENU[0])))
static int64_t s_banner_until;      // us; 0 = no banner up

static void face_save(int idx)
{
    nvs_handle_t h;
    if (nvs_open("fet", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_i32(h, "face", idx);
        nvs_commit(h);
        nvs_close(h);
    }
}

static void face_load(void)
{
    nvs_handle_t h;
    int32_t idx = -1;
    if (nvs_open("fet", NVS_READONLY, &h) == ESP_OK) {
        nvs_get_i32(h, "face", &idx);
        nvs_close(h);
    }
    if (idx >= 0 && idx < TARGET_COUNT) {
        model_set_target(&TARGETS[idx]);
        ESP_LOGI(TAG, "face: %s (saved choice)", TARGETS[idx].name);
    }
}

static void face_next(void)
{
    model_lock();
    int cur = (int)(g_model.target - TARGETS);
    model_unlock();
    int pos = -1;
    for (int i = 0; i < FACE_MENU_N; i++) if (FACE_MENU[i] == cur) pos = i;
    int idx = FACE_MENU[(pos + 1) % FACE_MENU_N];
    model_set_target(&TARGETS[idx]);
    face_save(idx);
    model_set_banner(TARGETS[idx].name);
    s_banner_until = esp_timer_get_time() + 2500 * 1000;
    ESP_LOGI(TAG, "face: %s (picked)", TARGETS[idx].name);
}

// BOOT button: tap = zoom, hold 1 s = next face, hold 4 s = clear shots.
static void button_poll(void)
{
    static int held_ms = 0;
    static bool cleared = false;
    bool down = gpio_get_level(PIN_BOOT) == 0;
    if (down) {
        held_ms += 20;
        if (held_ms >= 4000 && !cleared) {
            model_clear_shots();
            cleared = true;
        }
    } else {
        if (!cleared) {
            if (held_ms >= 1000) face_next();
            else if (held_ms > 30) model_toggle_zoom();
        }
        held_ms = 0;
        cleared = false;
    }
    if (s_banner_until && esp_timer_get_time() > s_banner_until) {
        s_banner_until = 0;
        model_set_banner(NULL);
    }
}

static void ui_task(void *arg)
{
    (void)arg;
    uint32_t drawn = 0;
    bool blink = false;
    int tick = 0;
    for (;;) {
        button_poll();
        if (++tick >= 25) {                          // ~0.5 s: blink while searching
            tick = 0;
            blink = !blink;
            model_snapshot(&s_snap);
            if (s_snap.net != NET_LIVE && s_snap.version == drawn) {
                draw(UI_STATUS_X, UI_STATUS_Y, UI_STATUS_W, UI_STATUS_H, blink);
            }
        }
        model_lock();
        bool changed = (g_model.version != drawn);
        model_unlock();
        if (changed) {
            model_snapshot(&s_snap);
            int64_t t0 = esp_timer_get_time();
            draw(0, 0, UI_W, UI_H, blink);
            drawn = s_snap.version;
            ESP_LOGI(TAG, "frame v%lu drawn in %lld ms", (unsigned long)drawn,
                     (esp_timer_get_time() - t0) / 1000);
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "freETarget CYD display");
    esp_err_t err = nvs_flash_init();                // WiFi needs NVS
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    s_lock = xSemaphoreCreateMutex();
    model_init();
    face_load();

    gpio_config_t b = {
        .pin_bit_mask = 1ULL << PIN_BOOT, .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&b);

    s_strip = heap_caps_malloc(UI_W * STRIP_H * sizeof(uint16_t), MALLOC_CAP_DMA);
    if (!s_strip || !lcd_init()) {
        ESP_LOGE(TAG, "display init failed");
    } else {
        xTaskCreate(ui_task, "ui", 6144, NULL, 4, NULL);
    }
    sd_start();                                      // optional shot log on microSD
    net_start();
}
