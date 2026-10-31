// Shot log on the CYD's microSD card (optional). ESP-IDF.
//
// If a FAT32 card is in the slot, every shot is appended to /FET/FETnnnn.CSV,
// one file per session. No card = nothing happens; a card inserted later is
// picked up within a few seconds and the shots already on screen are written.
#pragma once

#include "model.h"

void sd_start(void);
// Queue a scored shot for the log. Never blocks.
void sd_log_shot(const shot_t *s, const target_def_t *face);
