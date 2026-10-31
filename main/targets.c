#include "targets.h"
#include "config.h"

#include <ctype.h>
#include <math.h>
#include <stddef.h>
#include <string.h>

const target_def_t TARGETS[] = {
    {   // ISSF 50 m smallbore rifle. freETarget: Rifle50M.cs (class Rifle50m)
        .name = "50M RIFLE", .match = "Rifle50m",
        .ring_dia = { 154.4f, 138.4f, 122.4f, 106.4f, 90.4f, 74.4f, 58.4f, 42.4f, 26.4f, 10.4f },
        .inner_dia = 5.0f, .black_dia = 112.4f, .caliber = 5.6f, .decimal = true,
        .rule = SCORE_LINEAR, .inner = INNER_RING_PLUS_BULLET, .solid_dot = false,
    },
    {   // NSRA 25 yard smallbore rifle (UK). freETarget: NSRA_25Y.cs
        .name = "NSRA 25YD", .match = "NSRA_25Y",
        .ring_dia = { 78.75f, 71.44f, 64.12f, 56.81f, 49.49f, 42.18f, 34.86f, 27.55f, 20.23f, 12.92f },
        .inner_dia = 1.0f, .black_dia = 51.39f, .caliber = 5.6f, .decimal = false,
        .rule = SCORE_RINGS, .inner = INNER_FIXED_RADIUS, .solid_dot = false,
    },
    {   // ISSF 10 m air rifle. freETarget: AirRifle.cs
        .name = "10M AIR", .match = "AirRifle",
        .ring_dia = { 45.5f, 40.5f, 35.5f, 30.5f, 25.5f, 20.5f, 15.5f, 10.5f, 5.5f, 0.5f },
        .inner_dia = 0.5f, .black_dia = 30.5f, .caliber = 4.5f, .decimal = true,
        .rule = SCORE_LINEAR, .inner = INNER_BULLET_MINUS_DOT, .solid_dot = true,
    },
    {   // ISSF 10 m air pistol. freETarget: AirPistol.cs. The PC app's pistol
        // practice and match events score whole rings (decimal only in finals).
        .name = "10M PISTOL", .match = "AirPistol",
        .ring_dia = { 155.5f, 139.5f, 123.5f, 107.5f, 91.5f, 75.5f, 59.5f, 43.5f, 27.5f, 11.5f },
        .inner_dia = 5.0f, .black_dia = 59.5f, .caliber = 4.5f, .decimal = false,
        .rule = SCORE_LINEAR, .inner = INNER_RING_PLUS_BULLET, .solid_dot = false,
    },
};
const int TARGET_COUNT = sizeof(TARGETS) / sizeof(TARGETS[0]);

const target_def_t *target_default(void)
{
    return &TARGETS[FET_DEFAULT_TARGET];
}

static bool contains_nocase(const char *hay, const char *needle)
{
    size_t n = strlen(needle);
    for (; *hay; hay++) {
        size_t i = 0;
        while (i < n && hay[i] && tolower((unsigned char)hay[i]) == tolower((unsigned char)needle[i])) {
            i++;
        }
        if (i == n) {
            return true;
        }
    }
    return false;
}

const target_def_t *target_find(const char *target_name)
{
    if (!target_name || !*target_name) {
        return NULL;
    }
    for (int i = 0; i < TARGET_COUNT; i++) {
        if (contains_nocase(target_name, TARGETS[i].match)) {
            return &TARGETS[i];
        }
    }
    return NULL;
}

float target_caliber(const target_def_t *t)
{
    return FET_CALIBER_MM > 0 ? (float)FET_CALIBER_MM : t->caliber;
}

bool target_decimal(const target_def_t *t)
{
    return FET_DECIMAL < 0 ? t->decimal : FET_DECIMAL != 0;
}

static double inner_radius(const target_def_t *t, double cal)
{
    switch (t->inner) {
    case INNER_BULLET_MINUS_DOT: return cal / 2 - t->ring_dia[9] / 2.0;
    case INNER_FIXED_RADIUS:     return t->inner_dia;
    default:                     return t->inner_dia / 2.0 + cal / 2;
    }
}

// Tiny tolerance so exact ring-line hits land the way the PC app's decimal
// arithmetic puts them (it scores the higher value on a tie).
#define EPS 1e-9

score_t target_score(const target_def_t *t, double r)
{
    score_t s = { 0, 0, false };
    double cal = target_caliber(t);

    if (t->rule == SCORE_LINEAR) {
        double r10 = t->ring_dia[9] / 2.0 + cal / 2;
        double v = (11.0 - r / r10) * 10.0;            // decimal score x10
        int tenths = (int)floor(v + EPS);
        if (tenths > 109) tenths = 109;
        if (tenths < 10) tenths = 0;                   // below 1.0 = off the target
        s.tenths = tenths;
        s.ring = tenths / 10;
    } else {
        for (int ring = 10; ring >= 1; ring--) {
            if (r <= t->ring_dia[ring - 1] / 2.0 + cal / 2 + EPS) {
                s.ring = ring;
                break;
            }
        }
        s.tenths = s.ring * 10;
    }
    s.inner = (s.ring > 0) && (r <= inner_radius(t, cal) + EPS);
    return s;
}
