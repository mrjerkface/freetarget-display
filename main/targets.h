// Target faces and scoring. Pure C (no ESP-IDF).
//
// Ring sizes and scoring follow the freETarget PC client exactly
// (Software/C#/freETarget/targets/*.cs and Shot.cs computeScore):
//   - LINEAR faces (aTarget.getScore default): score = 11 - r / R10, where
//     R10 = ten-ring radius + bullet radius; truncated to one decimal,
//     capped at 10.9, below 1.0 -> 0.
//   - RINGS faces (NSRA override): whole rings; ring k if r <= ring_k radius
//     + bullet radius.
//   - Inner ten: r <= the face's getInnerTenRadius().
#pragma once

#include <stdbool.h>

typedef enum { SCORE_LINEAR, SCORE_RINGS } score_rule_t;

typedef enum {
    INNER_RING_PLUS_BULLET,  // inner_dia/2 + caliber/2        (Rifle50m, AirPistol)
    INNER_BULLET_MINUS_DOT,  // caliber/2 - ring10/2           (AirRifle: dot shot out)
    INNER_FIXED_RADIUS,      // inner_dia used as the radius   (NSRA_25Y, as the PC app does)
} inner_rule_t;

typedef struct {
    const char  *name;       // short label shown on screen, e.g. "50M RIFLE"
    const char  *match;      // substring of the target's "target_name" (case-insensitive)
    float        ring_dia[10]; // outer diameter of ring 1..10, mm
    float        inner_dia;  // inner ten ring diameter, mm
    float        black_dia;  // aiming black diameter, mm
    float        caliber;    // default bullet diameter, mm
    bool         decimal;    // default for decimal scoring display
    score_rule_t rule;
    inner_rule_t inner;
    bool         solid_dot;  // ten ring painted as a solid white dot (air rifle)
} target_def_t;

typedef struct {
    int  ring;               // 0 = miss, 1..10 (whole score)
    int  tenths;             // decimal score x10 (0..109)
    bool inner;              // inner ten / X
} score_t;

extern const target_def_t TARGETS[];
extern const int TARGET_COUNT;

const target_def_t *target_default(void);
const target_def_t *target_find(const char *target_name);

float target_caliber(const target_def_t *t);   // FET_CALIBER_MM or the face default
bool  target_decimal(const target_def_t *t);   // FET_DECIMAL or the face default

// Score a shot from its distance to the centre (mm), as the PC app does.
score_t target_score(const target_def_t *t, double radius_mm);
