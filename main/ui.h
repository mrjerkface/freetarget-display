// Screen layout and drawing. Pure C: renders RGB565 pixels into a buffer,
// which the display driver sends to the CYD in strips.
//
// 320x240 landscape:
//   x   0..239  the target face (auto-zooms to fit the group)
//   x 240..319  side panel: connection, last score, total, recent shots
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "model.h"

#define UI_W 320
#define UI_H 240

// Draw the region (x0,y0,w,h) of the screen into buf (w*h native RGB565,
// row-major). `blink` toggles the status dot while searching.
void ui_render(const model_t *m, bool blink, int x0, int y0, int w, int h, uint16_t *buf);

// The status-dot area, redrawn on its own for the blink.
#define UI_STATUS_X 240
#define UI_STATUS_Y 0
#define UI_STATUS_W 80
#define UI_STATUS_H 22
