#include "model.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

model_t g_model;

void model_init(void)
{
    model_lock();
    memset(&g_model, 0, sizeof(g_model));
    g_model.target = target_default();
    g_model.version = 1;
    model_unlock();
}

void model_set_net(net_state_t s, const char *ssid, int rssi)
{
    model_lock();
    if (g_model.net != s || g_model.rssi != rssi ||
        (ssid && strncmp(g_model.ssid, ssid, sizeof(g_model.ssid)) != 0)) {
        g_model.net = s;
        g_model.rssi = rssi;
        if (ssid) {
            strncpy(g_model.ssid, ssid, sizeof(g_model.ssid) - 1);
            g_model.ssid[sizeof(g_model.ssid) - 1] = '\0';
        }
        g_model.version++;
    }
    model_unlock();
}

void model_clear_shots(void)
{
    model_lock();
    g_model.count = 0;
    g_model.version++;
    model_unlock();
}

void model_toggle_zoom(void)
{
    model_lock();
    g_model.zoom = (g_model.zoom == ZOOM_AUTO) ? ZOOM_FULL : ZOOM_AUTO;
    g_model.version++;
    model_unlock();
}

// --- the PC app's shot math (frmMainWindow.getScaledDimension, Shot.cs) -------

// getScaledDimension: 100 * input / targetDistance, rounded to 9 decimals.
static double scale_dim(double v)
{
    double r = 100.0 * v / FET_DISTANCE_PERCENT;
    return round(r * 1e9) / 1e9;
}

// RotatePoint() about the origin, in float like the PC app's PointF.
static void rotate(float px, float py, float deg, float *ox, float *oy)
{
    double a = deg * (M_PI / 180.0);
    double c = cos(a), s = sin(a);
    *ox = (float)(c * px - s * py);
    *oy = (float)(s * px + c * py);
}

// The PC app casts the float radius to C# decimal, which keeps 7 significant
// digits. Doing the same makes ring-line ties come out identical.
static double float_to_decimal7(float f)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%.7g", (double)f);
    return strtod(buf, NULL);
}

static void place_and_score(shot_t *s, const target_def_t *t)
{
    const double cx = FET_CAL_X_MM, cy = FET_CAL_Y_MM;
    const float  ang = (float)FET_CAL_ANGLE_DEG;
    float dummy;
    // getX(): rotate (x + calX, y) and keep X;  getY(): rotate (x, y + calY) and keep Y.
    rotate((float)(s->sx + cx), (float)s->sy, ang, &s->x, &dummy);
    rotate((float)s->sx, (float)(s->sy + cy), ang, &dummy, &s->y);
    // recomputeRadiusFromXY(): offsets, no rotation; returned as float.
    float r = (float)sqrt((s->sx + cx) * (s->sx + cx) + (s->sy + cy) * (s->sy + cy));
    s->radius = float_to_decimal7(r);
    if (s->miss) {
        s->score.ring = 0;
        s->score.tenths = 0;
        s->score.inner = false;
    } else {
        s->score = target_score(t, s->radius);
    }
}

void model_set_target(const target_def_t *t)
{
    model_lock();
    g_model.face_user = true;
    if (t && t != g_model.target) {
        g_model.target = t;
        for (int i = 0; i < g_model.count; i++) {
            place_and_score(&g_model.shots[i], t);
        }
    }
    g_model.version++;
    model_unlock();
}

void model_set_banner(const char *text)
{
    model_lock();
    if (!text) text = "";
    if (strncmp(g_model.banner, text, sizeof(g_model.banner)) != 0) {
        snprintf(g_model.banner, sizeof(g_model.banner), "%.23s", text);
        g_model.version++;
    }
    model_unlock();
}

void model_set_sd(sd_state_t s, const char *file)
{
    model_lock();
    if (!file) file = "";
    if (g_model.sd != s || strncmp(g_model.sd_file, file, sizeof(g_model.sd_file)) != 0) {
        g_model.sd = s;
        snprintf(g_model.sd_file, sizeof(g_model.sd_file), "%.15s", file);
        g_model.version++;
    }
    model_unlock();
}

void model_add_shot(int number, double x, double y, bool miss, const char *target_name,
                    shot_t *out)
{
    model_lock();

    const target_def_t *t = g_model.face_user ? NULL : target_find(target_name);
    if (t && t != g_model.target) {
        g_model.target = t;                        // face changed: rescore everything
        for (int i = 0; i < g_model.count; i++) {
            place_and_score(&g_model.shots[i], t);
        }
    }

    shot_t *slot = NULL;
    for (int i = 0; i < g_model.count; i++) {
        if (g_model.shots[i].number == number) {
            slot = &g_model.shots[i];              // replay of a shot we have
            break;
        }
    }
    if (!slot) {
        if (g_model.count > 0 && number < g_model.shots[g_model.count - 1].number) {
            g_model.count = 0;                     // numbering restarted: new session
        }
        if (g_model.count == FET_MAX_SHOTS) {      // full: drop the oldest
            memmove(&g_model.shots[0], &g_model.shots[1], sizeof(shot_t) * (FET_MAX_SHOTS - 1));
            g_model.count--;
        }
        slot = &g_model.shots[g_model.count++];
    }
    slot->number = number;
    slot->miss = miss;
    slot->rx = x;
    slot->ry = y;
    slot->sx = scale_dim(x);
    slot->sy = scale_dim(y);
    place_and_score(slot, g_model.target);
    g_model.version++;
    if (out) *out = *slot;

    // One line per shot on the serial log, to compare with the PC app.
    char line[128];
    snprintf(line, sizeof(line), "score %d: %s r=%.2f mm %s%d.%d%s\n", number,
             g_model.target->name, slot->radius, miss ? "MISS " : "",
             slot->score.tenths / 10, slot->score.tenths % 10, slot->score.inner ? " X" : "");
    model_unlock();
    fputs(line, stdout);
}

void model_snapshot(model_t *out)
{
    model_lock();
    memcpy(out, &g_model, sizeof(*out));
    model_unlock();
}
