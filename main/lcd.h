// ILI9341 panel on the CYD (ESP32-2432S028R), landscape 320x240.
// Init sequence from TFT_eSPI (ILI9341_2_DRIVER); see Credits in the README.
#pragma once

#include <stdbool.h>
#include <stdint.h>

bool lcd_init(void);
// Send a w*h block of native RGB565 pixels to (x,y). Byte-swaps `px` in place.
void lcd_blit(int x, int y, int w, int h, uint16_t *px);
