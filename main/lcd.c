#include "lcd.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"

static const char *TAG = "lcd";

#define LCD_HOST  SPI2_HOST
#define PIN_SCLK  14
#define PIN_MOSI  13
#define PIN_MISO  12
#define PIN_CS    15
#define PIN_DC    2
#define PIN_BL    21

// Largest single transfer: one 320x24 strip.
#define MAX_XFER  4096

static spi_device_handle_t s_spi;

static void lcd_cmd(uint8_t c)
{
    gpio_set_level(PIN_DC, 0);
    spi_transaction_t t = { .length = 8, .tx_buffer = &c };
    spi_device_polling_transmit(s_spi, &t);
}

// Pixel data goes out in 1 KB chunks.
#define CHUNK 1024

static void lcd_data(const void *d, int len)
{
    static int errs = 0;
    const uint8_t *p = d;
    gpio_set_level(PIN_DC, 1);
    while (len > 0) {
        int n = len < CHUNK ? len : CHUNK;
        spi_transaction_t t = { .length = (size_t)n * 8, .tx_buffer = p };
        esp_err_t err = spi_device_polling_transmit(s_spi, &t);
        if (err != ESP_OK && errs++ < 5) {
            ESP_LOGE(TAG, "spi write of %d bytes failed: %s", n, esp_err_to_name(err));
        }
        p += n;
        len -= n;
    }
}

static void lcd_data1(uint8_t b) { lcd_data(&b, 1); }

static void set_window(int x0, int y0, int x1, int y1)
{
    uint8_t c[4];
    lcd_cmd(0x2A);
    c[0] = x0 >> 8; c[1] = x0 & 0xFF; c[2] = x1 >> 8; c[3] = x1 & 0xFF;
    lcd_data(c, 4);
    lcd_cmd(0x2B);
    c[0] = y0 >> 8; c[1] = y0 & 0xFF; c[2] = y1 >> 8; c[3] = y1 & 0xFF;
    lcd_data(c, 4);
    lcd_cmd(0x2C);
}

static void panel_init(void)
{
    // The CYD's panel wants TFT_eSPI's "ILI9341_2_DRIVER" init (the community
    // CYD reference config, github.com/witnessmenow/ESP32-Cheap-Yellow-Display),
    // not the classic Adafruit table, which leaves it washed-out grey.
    static const uint8_t seq[] = {
        0xCF, 3, 0x00, 0xC1, 0x30,
        0xED, 4, 0x64, 0x03, 0x12, 0x81,
        0xE8, 3, 0x85, 0x00, 0x78,
        0xCB, 5, 0x39, 0x2C, 0x00, 0x34, 0x02,
        0xF7, 1, 0x20,
        0xEA, 2, 0x00, 0x00,
        0xC0, 1, 0x10,                 // power control 1
        0xC1, 1, 0x00,                 // power control 2
        0xC5, 2, 0x30, 0x30,           // VCOM control 1
        0xC7, 1, 0xB7,                 // VCOM control 2
        0x3A, 1, 0x55,                 // 16-bit pixels
        0x36, 1, 0x28,                 // MADCTL: landscape (TFT_eSPI rotation 1), BGR
        0xB1, 2, 0x00, 0x1A,           // frame rate
        0xB6, 3, 0x08, 0x82, 0x27,     // display function control
        0xF2, 1, 0x00,                 // 3-gamma off
        0x26, 1, 0x01,                 // gamma curve 1
        0xE0, 15, 0x0F,0x2A,0x28,0x08,0x0E,0x08,0x54,0xA9,0x43,0x0A,0x0F,0x00,0x00,0x00,0x00,
        0xE1, 15, 0x00,0x15,0x17,0x07,0x11,0x06,0x2B,0x56,0x3C,0x05,0x10,0x0F,0x3F,0x3F,0x0F,
        0x00
    };
    lcd_cmd(0x01);                     // software reset (no RST pin on the CYD)
    vTaskDelay(pdMS_TO_TICKS(150));
    const uint8_t *p = seq;
    while (*p) {
        uint8_t cmd = *p++, n = *p++;
        lcd_cmd(cmd);
        for (uint8_t i = 0; i < n; i++) {
            lcd_data1(*p++);
        }
    }
    lcd_cmd(0x11);                     // sleep out
    vTaskDelay(pdMS_TO_TICKS(120));
    lcd_cmd(0x29);                     // display on
}

bool lcd_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << PIN_DC) | (1ULL << PIN_BL),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io);
    gpio_set_level(PIN_BL, 0);

    spi_bus_config_t bus = {
        .mosi_io_num = PIN_MOSI, .miso_io_num = PIN_MISO, .sclk_io_num = PIN_SCLK,
        .quadwp_io_num = -1, .quadhd_io_num = -1, .max_transfer_sz = MAX_XFER,
    };
    esp_err_t err = spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_initialize: %s", esp_err_to_name(err));
        return false;
    }
    spi_device_interface_config_t dev = {
        .clock_speed_hz = 40 * 1000 * 1000, .mode = 0, .spics_io_num = PIN_CS, .queue_size = 2,
    };
    err = spi_bus_add_device(LCD_HOST, &dev, &s_spi);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_add_device: %s", esp_err_to_name(err));
        return false;
    }
    panel_init();
    gpio_set_level(PIN_BL, 1);
    return true;
}

void lcd_blit(int x, int y, int w, int h, uint16_t *px)
{
    int n = w * h;
    for (int i = 0; i < n; i++) {
        px[i] = (uint16_t)((px[i] >> 8) | (px[i] << 8));   // panel wants high byte first
    }
    set_window(x, y, x + w - 1, y + h - 1);
    lcd_data(px, n * 2);
}
