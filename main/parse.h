// Pulls shot messages out of the target's TCP byte stream. Pure C.
//
// The ESP32 target sends   {{{{{"shot":3, "session_type":2, "time":12, "time_to_go":0,
//                           "x":1.25, "y":-3.10, "athelete":"", "event":"", "target_name":"..."}
// with no newline, and older Arduino targets send loose JSON with doubled
// commas. So the parser does not need valid JSON: it finds "shot", reads up to
// the next '}', and picks out the keys it needs.
#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    int   number;
    bool  miss;
    bool  has_xy;
    double x, y;       // mm as sent (two decimals)
    char  target_name[64];
} parsed_shot_t;

typedef void (*shot_cb_t)(const parsed_shot_t *shot, void *ctx);

typedef struct {
    char   buf[1024];
    size_t len;
} parser_t;

void parser_reset(parser_t *p);
void parser_feed(parser_t *p, const char *data, size_t len, shot_cb_t cb, void *ctx);
