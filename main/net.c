#include "net.h"

#include <errno.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

#include "config.h"
#include "model.h"
#include "parse.h"
#include "sd.h"

static const char *TAG = "net";

#define BIT_GOT_IP  BIT0
#define BIT_DOWN    BIT1

#define SCAN_MAX        20
#define JOIN_TIMEOUT_MS 15000
#define TCP_TRIES       5
#define FALLBACK_HOST   "192.168.10.9"     // freETarget's own access-point address

static EventGroupHandle_t s_ev;
static esp_netif_t       *s_netif;
static parser_t           s_parser;

static void on_wifi(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_ev, BIT_GOT_IP);
        xEventGroupSetBits(s_ev, BIT_DOWN);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupClearBits(s_ev, BIT_DOWN);
        xEventGroupSetBits(s_ev, BIT_GOT_IP);
    }
}

// Pick the network: the configured SSID, else the strongest FET-* one.
static bool scan_for_target(char ssid[33], int *rssi)
{
    wifi_scan_config_t cfg = { .show_hidden = false };
    if (esp_wifi_scan_start(&cfg, true) != ESP_OK) {
        return false;
    }
    static wifi_ap_record_t recs[SCAN_MAX];
    uint16_t n = SCAN_MAX;
    if (esp_wifi_scan_get_ap_records(&n, recs) != ESP_OK) {
        return false;
    }
    int best = -1;
    size_t plen = strlen(FET_SSID_PREFIX);
    for (int i = 0; i < n; i++) {
        const char *s = (const char *)recs[i].ssid;
        bool ok = FET_SSID[0] ? strcmp(s, FET_SSID) == 0 : strncmp(s, FET_SSID_PREFIX, plen) == 0;
        if (ok && (best < 0 || recs[i].rssi > recs[best].rssi)) {
            best = i;
        }
    }
    if (best < 0) {
        ESP_LOGI(TAG, "scan: %d networks, no target", n);
        return false;
    }
    memcpy(ssid, recs[best].ssid, 32);
    ssid[32] = '\0';
    *rssi = recs[best].rssi;
    ESP_LOGI(TAG, "scan: picked %s (%d dBm)", ssid, *rssi);
    return true;
}

static bool join(const char *ssid)
{
    wifi_config_t wc = { 0 };
    // The driver's fields are fixed-size and need no terminator when full.
    memcpy(wc.sta.ssid, ssid, strnlen(ssid, sizeof(wc.sta.ssid)));
    memcpy(wc.sta.password, FET_PASSWORD, strnlen(FET_PASSWORD, sizeof(wc.sta.password)));
    wc.sta.threshold.authmode = FET_PASSWORD[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    esp_wifi_disconnect();
    xEventGroupClearBits(s_ev, BIT_GOT_IP | BIT_DOWN);
    esp_wifi_set_config(WIFI_IF_STA, &wc);
    if (esp_wifi_connect() != ESP_OK) {
        return false;
    }
    EventBits_t b = xEventGroupWaitBits(s_ev, BIT_GOT_IP | BIT_DOWN, pdFALSE, pdFALSE,
                                        pdMS_TO_TICKS(JOIN_TIMEOUT_MS));
    return (b & BIT_GOT_IP) != 0;
}

static int current_rssi(void)
{
    wifi_ap_record_t ap;
    return esp_wifi_sta_get_ap_info(&ap) == ESP_OK ? ap.rssi : -100;
}

static void target_addr(char out[16])
{
    if (FET_HOST[0]) {
        strncpy(out, FET_HOST, 15);
        out[15] = '\0';
        return;
    }
    esp_netif_ip_info_t ip;
    if (esp_netif_get_ip_info(s_netif, &ip) == ESP_OK && ip.gw.addr != 0) {
        esp_ip4addr_ntoa(&ip.gw, out, 16);
    } else {
        strcpy(out, FALLBACK_HOST);
    }
}

static int tcp_open(const char *host)
{
    int s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s < 0) return -1;

    struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    struct timeval stv = { .tv_sec = 4, .tv_usec = 0 };
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &stv, sizeof(stv));
    // Notice a target that was switched off within ~15 s.
    int on = 1, idle = 5, intvl = 2, cnt = 4;
    setsockopt(s, SOL_SOCKET, SO_KEEPALIVE, &on, sizeof(on));
    setsockopt(s, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle));
    setsockopt(s, IPPROTO_TCP, TCP_KEEPINTVL, &intvl, sizeof(intvl));
    setsockopt(s, IPPROTO_TCP, TCP_KEEPCNT, &cnt, sizeof(cnt));

    struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons(FET_PORT) };
    inet_pton(AF_INET, host, &a.sin_addr);
    if (connect(s, (struct sockaddr *)&a, sizeof(a)) != 0) {
        ESP_LOGW(TAG, "connect %s:%d failed: errno %d", host, FET_PORT, errno);
        close(s);
        return -1;
    }
    return s;
}

static void on_shot(const parsed_shot_t *sh, void *ctx)
{
    (void)ctx;
    if (!sh->miss && !sh->has_xy) {
        ESP_LOGW(TAG, "shot %d: no x/y, ignored", sh->number);
        return;
    }
    ESP_LOGI(TAG, "shot %d: %s x=%.2f y=%.2f %s", sh->number, sh->miss ? "MISS" : "hit",
             sh->x, sh->y, sh->target_name);
    // Like the PC app, a reported miss counts as a shot scoring 0.
    shot_t s;
    model_add_shot(sh->number, sh->x, sh->y, sh->miss, sh->target_name, &s);
    model_lock();
    const target_def_t *face = g_model.target;
    model_unlock();
    sd_log_shot(&s, face);
}

// Read until the link drops. Returns when the socket or WiFi goes away.
static void stream(int s)
{
    char buf[512];
    TickType_t last_rssi = 0;
    for (;;) {
        if (xEventGroupGetBits(s_ev) & BIT_DOWN) {
            ESP_LOGW(TAG, "wifi dropped");
            return;
        }
        int n = recv(s, buf, sizeof(buf), 0);
        if (n > 0) {
            parser_feed(&s_parser, buf, (size_t)n, on_shot, NULL);
        } else if (n == 0) {
            ESP_LOGW(TAG, "target closed the connection");
            return;
        } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
            ESP_LOGW(TAG, "recv errno %d", errno);
            return;
        }
        if (xTaskGetTickCount() - last_rssi > pdMS_TO_TICKS(3000)) {
            last_rssi = xTaskGetTickCount();
            model_set_net(NET_LIVE, NULL, current_rssi());
        }
    }
}

static void net_task(void *arg)
{
    (void)arg;
    char ssid[33] = "";
    int rssi = 0;
    for (;;) {
        model_set_net(NET_SCAN, "", 0);
        if (!scan_for_target(ssid, &rssi)) {
            vTaskDelay(pdMS_TO_TICKS(1500));
            continue;
        }
        model_set_net(NET_JOIN, ssid, rssi);
        if (!join(ssid)) {
            ESP_LOGW(TAG, "could not join %s", ssid);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        esp_wifi_set_ps(WIFI_PS_NONE);            // lowest latency for shots

        // Joined. Keep (re)opening TCP while the WiFi stays up.
        int fails = 0;
        while (fails < TCP_TRIES && !(xEventGroupGetBits(s_ev) & BIT_DOWN)) {
            char host[16];
            target_addr(host);
            model_set_net(NET_LINK, ssid, current_rssi());
            ESP_LOGI(TAG, "connecting to %s:%d", host, FET_PORT);
            int s = tcp_open(host);
            if (s < 0) {
                fails++;
                vTaskDelay(pdMS_TO_TICKS(2000));
                continue;
            }
            fails = 0;
            // The target replays every stored shot on connect, so start fresh.
            parser_reset(&s_parser);
            model_clear_shots();
            model_set_net(NET_LIVE, ssid, current_rssi());
            ESP_LOGI(TAG, "live");
            stream(s);
            close(s);
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        esp_wifi_disconnect();
    }
}

void net_start(void)
{
    s_ev = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) ESP_ERROR_CHECK(err);
    s_netif = esp_netif_create_default_wifi_sta();

    wifi_init_config_t ic = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&ic));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, on_wifi, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    xTaskCreate(net_task, "net", 6144, NULL, 5, NULL);
}
