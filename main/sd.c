#include "sd.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

static const char *TAG = "sd";

// microSD slot of the ESP32-2432S028R, on its own SPI bus (the panel is on SPI2).
#define SD_HOST     SPI3_HOST
#define SD_CLK      18
#define SD_MOSI     23
#define SD_MISO     19
#define SD_CS       5

#define MOUNT       "/sdcard"
#define LOG_DIR     MOUNT "/FET"
#define RETRY_US    (5 * 1000 * 1000)   // look for a card / check it is still there
#define MAX_DONE    500                 // shots remembered per file, to skip replays

typedef struct {
    shot_t              s;
    const target_def_t *face;
    int64_t             ms;             // uptime when received; -1 = unknown
} rec_t;

static QueueHandle_t  s_q;
static sdmmc_card_t  *s_card;
static bool           s_bus;
static FILE          *s_fp;
static int            s_file_no;        // highest FETnnnn.CSV on the card
static char           s_name[24];       // current file, "" = none yet
static struct { int n; float x, y; } s_done[MAX_DONE];
static int            s_ndone;

// ---------------------------------------------------------------------------
// Mounting
// ---------------------------------------------------------------------------
static void quiet_driver_logs(void)
{
    // A missing card makes the driver log errors on every retry; we report the
    // outcome ourselves.
    static const char *tags[] = { "sdmmc_common", "sdmmc_init", "sdmmc_cmd", "sdspi_host",
                                  "sdspi_transaction", "vfs_fat_sdmmc" };
    for (size_t i = 0; i < sizeof(tags) / sizeof(tags[0]); i++) {
        esp_log_level_set(tags[i], ESP_LOG_NONE);
    }
}

static esp_err_t bus_init(void)
{
    if (s_bus) return ESP_OK;
    spi_bus_config_t bus = {
        .mosi_io_num = SD_MOSI, .miso_io_num = SD_MISO, .sclk_io_num = SD_CLK,
        .quadwp_io_num = -1, .quadhd_io_num = -1, .max_transfer_sz = 4000,
    };
    esp_err_t err = spi_bus_initialize(SD_HOST, &bus, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) return err;
    // SD cards need pull-ups on their data lines; the CYD has none on MISO.
    gpio_pullup_en(SD_MISO);
    gpio_pullup_en(SD_MOSI);
    gpio_pullup_en(SD_CS);
    s_bus = true;
    return ESP_OK;
}

// Try to mount. Card setup always starts at 400 kHz, so a timeout there means
// no card. Any other failure steps the clock down, since some cards and
// sockets can't run at 20 MHz.
static esp_err_t try_mount(void)
{
    esp_err_t err = bus_init();
    if (err != ESP_OK) return err;
    static const int khz[] = { SDMMC_FREQ_DEFAULT, 10000, 4000, 1000 };
    for (size_t i = 0; i < sizeof(khz) / sizeof(khz[0]); i++) {
        sdmmc_host_t host = SDSPI_HOST_DEFAULT();
        host.slot = SD_HOST;
        host.max_freq_khz = khz[i];
        sdspi_device_config_t dev = SDSPI_DEVICE_CONFIG_DEFAULT();
        dev.gpio_cs = SD_CS;
        dev.host_id = SD_HOST;
        esp_vfs_fat_sdmmc_mount_config_t mc = {
            .format_if_mount_failed = false,   // never wipe someone's card
            .max_files = 3,
            .allocation_unit_size = 16 * 1024,
        };
        err = esp_vfs_fat_sdspi_mount(MOUNT, &host, &dev, &mc, &s_card);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "card mounted at %d kHz, %llu MB", khz[i],
                     (unsigned long long)s_card->csd.capacity * s_card->csd.sector_size / (1024 * 1024));
            return ESP_OK;
        }
        s_card = NULL;
        if (err == ESP_ERR_TIMEOUT || err == ESP_ERR_NOT_FOUND) return err;   // no card
        if (err == ESP_FAIL) return err;    // card talks, but no FAT filesystem
        ESP_LOGW(TAG, "mount at %d kHz failed: %s", khz[i], esp_err_to_name(err));
    }
    return err;
}

static void unmount(void)
{
    if (s_fp) { fclose(s_fp); s_fp = NULL; }
    if (s_card) { esp_vfs_fat_sdcard_unmount(MOUNT, s_card); s_card = NULL; }
    s_name[0] = '\0';
    s_ndone = 0;
}

// ---------------------------------------------------------------------------
// Files
// ---------------------------------------------------------------------------
static void path_of(char *out, size_t n, const char *name)
{
    snprintf(out, n, LOG_DIR "/%s", name);
}

static void remember(int number, double x, double y)
{
    if (s_ndone < MAX_DONE) {
        s_done[s_ndone].n = number;
        s_done[s_ndone].x = (float)x;
        s_done[s_ndone].y = (float)y;
        s_ndone++;
    }
}

// Find the newest FETnnnn.CSV and load its shot numbers and x/y, so the
// target's replay after a reboot or reconnect is not written twice.
static void scan_card(void)
{
    mkdir(LOG_DIR, 0775);
    s_file_no = 0;
    DIR *d = opendir(LOG_DIR);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            char up[16] = "";
            for (int i = 0; i < 15 && e->d_name[i]; i++) up[i] = (char)toupper((unsigned char)e->d_name[i]);
            int n;
            if (sscanf(up, "FET%4d.CSV", &n) == 1 && n > s_file_no) s_file_no = n;
        }
        closedir(d);
    }
    s_name[0] = '\0';
    s_ndone = 0;
    if (s_file_no == 0) return;

    snprintf(s_name, sizeof(s_name), "FET%04d.CSV", s_file_no);
    char path[48];
    path_of(path, sizeof(path), s_name);
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[160];
    while (fgets(line, sizeof(line), f)) {
        int n;
        double x, y;
        // shot,uptime_s,face,x_mm,y_mm,...
        if (sscanf(line, "%d,%*[^,],%*[^,],%lf,%lf", &n, &x, &y) == 3) remember(n, x, y);
    }
    fclose(f);
}

static bool new_file(void)
{
    if (s_fp) { fclose(s_fp); s_fp = NULL; }
    if (s_file_no >= 9999) return false;
    s_file_no++;
    snprintf(s_name, sizeof(s_name), "FET%04d.CSV", s_file_no);
    s_ndone = 0;
    char path[48];
    path_of(path, sizeof(path), s_name);
    s_fp = fopen(path, "w");
    if (!s_fp) return false;
    fputs("shot,uptime_s,face,x_mm,y_mm,plot_x_mm,plot_y_mm,radius_mm,score,inner,miss\n", s_fp);
    ESP_LOGI(TAG, "new log %s", path);
    return true;
}

static int find_done(int number)
{
    for (int i = s_ndone - 1; i >= 0; i--) {
        if (s_done[i].n == number) return i;
    }
    return -1;
}

static int max_done(void)
{
    int m = 0;
    for (int i = 0; i < s_ndone; i++) if (s_done[i].n > m) m = s_done[i].n;
    return m;
}

// Write one shot. False on a card error.
static bool write_rec(const rec_t *r)
{
    const shot_t *s = &r->s;
    int i = find_done(s->number);
    if (i >= 0) {
        if (fabsf(s_done[i].x - (float)s->rx) < 0.01f && fabsf(s_done[i].y - (float)s->ry) < 0.01f) {
            return true;                                  // replay of a shot we have
        }
        if (!new_file()) return false;                    // same number, new place: new session
    } else if (!s_name[0] || (s_ndone > 0 && s->number < max_done())) {
        if (!new_file()) return false;                    // first file, or numbering restarted
    }
    if (!s_fp) {
        char path[48];
        path_of(path, sizeof(path), s_name);
        s_fp = fopen(path, "a");
        if (!s_fp) return false;
    }

    char t[16] = "";
    if (r->ms >= 0) snprintf(t, sizeof(t), "%.1f", r->ms / 1000.0);
    char sc[16];
    if (target_decimal(r->face)) snprintf(sc, sizeof(sc), "%d.%d", s->score.tenths / 10, s->score.tenths % 10);
    else snprintf(sc, sizeof(sc), "%d", s->score.ring);
    int n = fprintf(s_fp, "%d,%s,%s,%.3f,%.3f,%.3f,%.3f,%.3f,%s,%d,%d\n",
                    s->number, t, r->face->name, s->rx, s->ry, s->x, s->y, s->radius, sc,
                    s->score.inner ? 1 : 0, s->miss ? 1 : 0);
    // Flush each shot to the card, so pulling the power loses nothing.
    if (n <= 0 || fflush(s_fp) != 0 || fsync(fileno(s_fp)) != 0) return false;
    remember(s->number, s->rx, s->ry);
    return true;
}

// Write the shots already on screen (fired before the card went in).
static bool backfill(void)
{
    static shot_t shots[FET_MAX_SHOTS];
    model_lock();
    int count = g_model.count;
    const target_def_t *face = g_model.target;
    memcpy(shots, g_model.shots, sizeof(shot_t) * count);
    model_unlock();
    for (int i = 0; i < count; i++) {
        rec_t r = { .s = shots[i], .face = face, .ms = -1 };
        if (!write_rec(&r)) return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
static void fail(const char *what)
{
    ESP_LOGE(TAG, "%s failed (card full or removed?)", what);
    unmount();
    model_set_sd(SD_ERR, "");
}

static void sd_task(void *arg)
{
    (void)arg;
    bool first = true;
    esp_err_t last = ESP_OK;
    int64_t next = 0;
    for (;;) {
        rec_t r;
        bool got = xQueueReceive(s_q, &r, pdMS_TO_TICKS(1000)) == pdTRUE;
        int64_t now = esp_timer_get_time();

        if (!s_card && now >= next) {
            next = now + RETRY_US;
            esp_err_t err = try_mount();
            if (first) { quiet_driver_logs(); first = false; }
            if (err == ESP_OK) {
                scan_card();
                model_set_sd(SD_OK, s_name);
                if (!backfill()) fail("write");
                else model_set_sd(SD_OK, s_name);
            } else if (err != last) {
                if (err == ESP_FAIL) {
                    ESP_LOGW(TAG, "card found but it is not FAT32; format it FAT32 to log shots");
                    model_set_sd(SD_NOFS, "");
                } else if (err == ESP_ERR_TIMEOUT || err == ESP_ERR_NOT_FOUND) {
                    ESP_LOGI(TAG, "no card; shots are not logged");
                    model_set_sd(SD_NONE, "");
                } else {
                    ESP_LOGW(TAG, "card not usable: %s", esp_err_to_name(err));
                    model_set_sd(SD_ERR, "");
                }
            }
            last = err;
        } else if (s_card && !got && now >= next) {
            next = now + RETRY_US;
            if (sdmmc_get_status(s_card) != ESP_OK) {     // card pulled out
                ESP_LOGW(TAG, "card removed");
                unmount();
                model_set_sd(SD_NONE, "");
                last = ESP_ERR_TIMEOUT;
            }
        }

        if (got && s_card) {
            if (write_rec(&r)) model_set_sd(SD_OK, s_name);
            else fail("write");
        }
    }
}

void sd_start(void)
{
    s_q = xQueueCreate(16, sizeof(rec_t));
    if (!s_q) return;
    xTaskCreate(sd_task, "sd", 4096, NULL, 2, NULL);
}

void sd_log_shot(const shot_t *s, const target_def_t *face)
{
    if (!s_q) return;
    rec_t r = { .s = *s, .face = face, .ms = esp_timer_get_time() / 1000 };
    xQueueSend(s_q, &r, 0);
}
