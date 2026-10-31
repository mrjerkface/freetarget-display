// Everything the screen shows, in one struct. Pure C.
//
// The network task writes it, the UI task reads it. Writers wrap changes in
// model_lock()/model_unlock() and bump `version`; the UI copies a snapshot
// under the lock and redraws when `version` changed. The lock functions are
// provided by the platform (a FreeRTOS mutex on the device).
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "config.h"
#include "targets.h"

typedef enum {
    NET_START = 0,
    NET_SCAN,          // looking for a FET-* network
    NET_JOIN,          // joining it
    NET_LINK,          // opening TCP to the target
    NET_LIVE,          // connected, shots stream in
} net_state_t;

typedef enum { ZOOM_AUTO = 0, ZOOM_FULL } zoom_t;

typedef enum {
    SD_NONE = 0,       // no card (nothing shown)
    SD_OK,             // logging to sd_file
    SD_NOFS,           // card answers but has no FAT filesystem (exFAT/NTFS)
    SD_ERR,            // card failed or a write failed
} sd_state_t;

typedef struct {
    int     number;    // shot number from the target (starts at 1)
    bool    miss;      // target reported a miss: scores 0, not drawn
    double  rx, ry;    // x/y exactly as the target sent them, mm
    double  sx, sy;    // received x/y after distance scaling, mm
    float   x, y;      // where it is drawn (PC app's getX()/getY()), mm, +y up
    double  radius;    // distance used for scoring (PC app's recomputeRadiusFromXY)
    score_t score;
} shot_t;

typedef struct {
    uint32_t            version;
    net_state_t         net;
    char                ssid[33];
    int                 rssi;
    const target_def_t *target;
    bool                face_user;     // picked on the CYD: ignore the target's face name
    char                banner[24];    // short message across the target, "" = none
    zoom_t              zoom;
    sd_state_t          sd;
    char                sd_file[16];   // e.g. "FET0003.CSV"
    int                 count;
    shot_t              shots[FET_MAX_SHOTS];
} model_t;

extern model_t g_model;

void model_lock(void);
void model_unlock(void);

// All of these take the lock themselves.
void model_init(void);
void model_set_net(net_state_t s, const char *ssid, int rssi);
void model_clear_shots(void);
void model_toggle_zoom(void);
// A shot from the target. Replays of a known shot number replace it; a lower
// number than the last one means the target started a new session.
// If `out` is not NULL it receives a copy of the scored shot.
void model_add_shot(int number, double x, double y, bool miss, const char *target_name,
                    shot_t *out);
void model_set_sd(sd_state_t s, const char *file);
// Face picked on the CYD: rescores every shot, and the target's face name
// is ignored from then on.
void model_set_target(const target_def_t *t);
void model_set_banner(const char *text);   // NULL or "" clears it
// Copy for drawing.
void model_snapshot(model_t *out);
