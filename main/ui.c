#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "font5x7.h"

// ---------------------------------------------------------------------------
// Colours and the canvas
// ---------------------------------------------------------------------------
typedef struct { uint8_t r, g, b; } rgb_t;

static const rgb_t C_PAPER   = { 236, 230, 212 };
static const rgb_t C_BLACK   = {  20,  20,  22 };
static const rgb_t C_LINE_DK = {  50,  48,  44 };
static const rgb_t C_LINE_LT = { 215, 215, 210 };
static const rgb_t C_NUM_DK  = {  90,  86,  78 };
static const rgb_t C_NUM_LT  = { 160, 160, 160 };
static const rgb_t C_SHOT    = {  40, 120, 235 };
static const rgb_t C_SHOT_OLD= { 105, 130, 175 };
static const rgb_t C_LAST    = { 235,  45,  40 };
static const rgb_t C_RIM     = { 250, 250, 250 };
static const rgb_t C_MPI     = {  30, 200,  90 };
static const rgb_t C_PANEL   = {  16,  20,  27 };
static const rgb_t C_RULE    = {  42,  50,  64 };
static const rgb_t C_LABEL   = { 120, 132, 150 };
static const rgb_t C_TEXT    = { 235, 238, 242 };
static const rgb_t C_DIM     = {  70,  80,  96 };
static const rgb_t C_GOLD    = { 255, 196,  40 };
static const rgb_t C_OK      = {  60, 210, 100 };
static const rgb_t C_WAIT    = { 245, 175,  35 };
static const rgb_t C_CHIP    = {   0,   0,   0 };

typedef struct {
    uint16_t *buf;
    int x0, y0, w, h;
} canvas_t;

static inline uint16_t pack(rgb_t c)
{
    return (uint16_t)(((c.r & 0xF8) << 8) | ((c.g & 0xFC) << 3) | (c.b >> 3));
}

static inline rgb_t unpack(uint16_t v)
{
    rgb_t c;
    c.r = (uint8_t)(((v >> 11) & 0x1F) * 255 / 31);
    c.g = (uint8_t)(((v >> 5) & 0x3F) * 255 / 63);
    c.b = (uint8_t)((v & 0x1F) * 255 / 31);
    return c;
}

static inline rgb_t mix(rgb_t a, rgb_t b, float t)
{
    rgb_t c = {
        (uint8_t)(a.r + (b.r - a.r) * t),
        (uint8_t)(a.g + (b.g - a.g) * t),
        (uint8_t)(a.b + (b.b - a.b) * t),
    };
    return c;
}

static inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

static inline void blend(canvas_t *c, int x, int y, rgb_t col, float a)
{
    if (a <= 0.0f || x < c->x0 || y < c->y0 || x >= c->x0 + c->w || y >= c->y0 + c->h) {
        return;
    }
    uint16_t *p = &c->buf[(y - c->y0) * c->w + (x - c->x0)];
    *p = (a >= 0.999f) ? pack(col) : pack(mix(unpack(*p), col, a));
}

// Clip a box to the canvas; false if nothing is left.
static bool clip(const canvas_t *c, int *x0, int *y0, int *x1, int *y1)
{
    if (*x0 < c->x0) *x0 = c->x0;
    if (*y0 < c->y0) *y0 = c->y0;
    if (*x1 > c->x0 + c->w) *x1 = c->x0 + c->w;
    if (*y1 > c->y0 + c->h) *y1 = c->y0 + c->h;
    return *x0 < *x1 && *y0 < *y1;
}

static void fill(canvas_t *c, int x, int y, int w, int h, rgb_t col, float a)
{
    int x0 = x, y0 = y, x1 = x + w, y1 = y + h;
    if (!clip(c, &x0, &y0, &x1, &y1)) return;
    for (int yy = y0; yy < y1; yy++)
        for (int xx = x0; xx < x1; xx++)
            blend(c, xx, yy, col, a);
}

// Rounded rectangle, anti-aliased corners.
static void rrect(canvas_t *c, int x, int y, int w, int h, float r, rgb_t col, float a)
{
    int x0 = x, y0 = y, x1 = x + w, y1 = y + h;
    if (!clip(c, &x0, &y0, &x1, &y1)) return;
    for (int yy = y0; yy < y1; yy++) {
        for (int xx = x0; xx < x1; xx++) {
            float px = xx + 0.5f, py = yy + 0.5f;
            float dx = fmaxf(fmaxf(x + r - px, px - (x + w - r)), 0);
            float dy = fmaxf(fmaxf(y + r - py, py - (y + h - r)), 0);
            float cov = clamp01(r - sqrtf(dx * dx + dy * dy) + 0.5f);
            blend(c, xx, yy, col, a * cov);
        }
    }
}

// Filled disc (cx,cy,r) in pixels, anti-aliased.
static void disc(canvas_t *c, float cx, float cy, float r, rgb_t col, float a)
{
    int x0 = (int)floorf(cx - r - 1), y0 = (int)floorf(cy - r - 1);
    int x1 = (int)ceilf(cx + r + 1), y1 = (int)ceilf(cy + r + 1);
    if (!clip(c, &x0, &y0, &x1, &y1)) return;
    for (int yy = y0; yy < y1; yy++) {
        for (int xx = x0; xx < x1; xx++) {
            float dx = xx + 0.5f - cx, dy = yy + 0.5f - cy;
            blend(c, xx, yy, col, a * clamp01(r - sqrtf(dx * dx + dy * dy) + 0.5f));
        }
    }
}

// Circle outline of radius r and line width lw, anti-aliased.
static void ring(canvas_t *c, float cx, float cy, float r, float lw, rgb_t col, float a)
{
    float o = r + lw;
    int x0 = (int)floorf(cx - o - 1), y0 = (int)floorf(cy - o - 1);
    int x1 = (int)ceilf(cx + o + 1), y1 = (int)ceilf(cy + o + 1);
    if (!clip(c, &x0, &y0, &x1, &y1)) return;
    float ri = r - lw - 1;
    for (int yy = y0; yy < y1; yy++) {
        for (int xx = x0; xx < x1; xx++) {
            float dx = xx + 0.5f - cx, dy = yy + 0.5f - cy;
            float d2 = dx * dx + dy * dy;
            if (ri > 0 && d2 < ri * ri) continue;
            float d = sqrtf(d2);
            blend(c, xx, yy, col, a * clamp01(lw / 2 + 0.5f - fabsf(d - r)));
        }
    }
}

// ---------------------------------------------------------------------------
// Text: 5x7 pixel font (labels) and smooth 7-segment digits (big numbers)
// ---------------------------------------------------------------------------
static int text_w(const char *s, int scale)
{
    int n = (int)strlen(s);
    return n ? n * 6 * scale - scale : 0;
}


static void text(canvas_t *c, int x, int y, const char *s, int scale, rgb_t color)
{
    for (; *s; s++, x += 6 * scale) {
        char ch = *s;
        if (ch >= 'a' && ch <= 'z') ch -= 32;
        if (ch < 0x20 || ch > 0x5A) ch = ' ';
        if (x > c->x0 + c->w || x + 6 * scale < c->x0) continue;
        const uint8_t *g = FONT[ch - 0x20];
        for (int gx = 0; gx < 5; gx++)
            for (int gy = 0; gy < 7; gy++)
                if (g[gx] & (1 << gy))
                    fill(c, x + gx * scale, y + gy * scale, scale, scale, color, 1.0f);
    }
}

// 7-segment digits drawn as rounded capsules. h = digit height in pixels.
//  aaa
// f   b
//  ggg
// e   c
//  ddd
static const uint8_t SEG[10] = {
    0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F,   // bit0=a .. bit6=g
};

typedef struct { float w, t, gap; } seg_metrics_t;

static seg_metrics_t seg_m(float h)
{
    seg_metrics_t m = { 0.50f * h, 0.15f * h, 0.20f * h };
    if (m.t < 2.0f) m.t = 2.0f;
    return m;
}

static float seg_adv(char ch, seg_metrics_t m)
{
    if (ch == '1') return m.t + m.gap;
    if (ch == '.') return m.t + m.gap * 0.7f;
    if (ch == ' ') return m.w * 0.5f;
    return m.w + m.gap;
}

static int seg_width(const char *s, float h)
{
    seg_metrics_t m = seg_m(h);
    float w = 0;
    for (const char *p = s; *p; p++) w += seg_adv(*p, m);
    return (int)ceilf(w - m.gap);
}

static float dist_seg(float px, float py, float ax, float ay, float bx, float by)
{
    float vx = bx - ax, vy = by - ay;
    float t = ((px - ax) * vx + (py - ay) * vy) / (vx * vx + vy * vy + 1e-6f);
    t = clamp01(t);
    float dx = px - (ax + vx * t), dy = py - (ay + vy * t);
    return sqrtf(dx * dx + dy * dy);
}

static void seg_glyph(canvas_t *c, float x, float y, float h, char ch, rgb_t color)
{
    seg_metrics_t m = seg_m(h);
    float ht = m.t / 2;
    if (ch == '.') {
        disc(c, x + ht, y + h - ht, ht * 1.05f, color, 1.0f);
        return;
    }
    uint8_t bits;
    if (ch >= '0' && ch <= '9') bits = SEG[ch - '0'];
    else if (ch == '-') bits = 0x40;
    else return;

    float L = x + ht, R = (ch == '1') ? x + ht : x + m.w - ht;
    float T = y + ht, M = y + h / 2, B = y + h - ht;
    float g = ht * 1.3f;                              // gap at the segment joints
    float seg[7][4] = {
        { L + g, T, R - g, T },                       // a
        { R, T + g, R, M - g },                       // b
        { R, M + g, R, B - g },                       // c
        { L + g, B, R - g, B },                       // d
        { L, M + g, L, B - g },                       // e
        { L, T + g, L, M - g },                       // f
        { L + g, M, R - g, M },                       // g
    };
    if (ch == '1') bits = 0x06;                       // b and c, drawn in the left column

    int x0 = (int)floorf(x - 1), y0 = (int)floorf(y - 1);
    int x1 = (int)ceilf(x + m.w + 1), y1 = (int)ceilf(y + h + 1);
    if (!clip(c, &x0, &y0, &x1, &y1)) return;
    for (int yy = y0; yy < y1; yy++) {
        for (int xx = x0; xx < x1; xx++) {
            float px = xx + 0.5f, py = yy + 0.5f, d = 1e9f;
            for (int i = 0; i < 7; i++) {
                if (bits & (1 << i)) {
                    float di = dist_seg(px, py, seg[i][0], seg[i][1], seg[i][2], seg[i][3]);
                    if (di < d) d = di;
                }
            }
            blend(c, xx, yy, color, clamp01(ht - d + 0.5f));
        }
    }
}

static void seg_text(canvas_t *c, float x, float y, float h, const char *s, rgb_t color)
{
    seg_metrics_t m = seg_m(h);
    for (; *s; s++) {
        seg_glyph(c, x, y, h, *s, color);
        x += seg_adv(*s, m);
    }
}

// ---------------------------------------------------------------------------
// Target face
// ---------------------------------------------------------------------------
#define TGT_SIZE 240
#define TGT_CX   120.0f
#define TGT_CY   120.0f

typedef struct {
    float scale;                 // pixels per mm
    float hole_r;                // bullet hole radius, px
} view_t;

static view_t compute_view(const model_t *m)
{
    const target_def_t *t = m->target;
    float br = target_caliber(t) / 2;
    float full = (t->ring_dia[0] / 2 + br) * 1.04f;       // whole face
    float min_auto = t->ring_dia[6] / 2;                  // never closer than ring 7
    float reach = 0;
    for (int i = 0; i < m->count; i++) {
        const shot_t *s = &m->shots[i];
        if (s->miss) continue;
        float d = sqrtf(s->x * s->x + s->y * s->y) + br;
        if (d > reach) reach = d;
    }
    float r;
    if (m->zoom == ZOOM_FULL || m->count == 0) {
        r = fmaxf(full, reach * 1.08f);
    } else {
        r = fmaxf(min_auto, reach * 1.15f);
    }
    if (r > full * 1.6f) r = full * 1.6f;                 // wild shots fall off the edge
    view_t v;
    v.scale = (TGT_SIZE / 2.0f - 0.5f) / r;
    v.hole_r = br * v.scale;
    return v;
}

static void draw_face(canvas_t *c, const model_t *m, const view_t *v)
{
    const target_def_t *t = m->target;
    float black_r = t->black_dia / 2 * v->scale;
    float rr[11];
    int nr = 0;
    for (int i = 0; i < 10; i++) rr[nr++] = t->ring_dia[i] / 2 * v->scale;
    if (t->inner_dia > 0 && !t->solid_dot) rr[nr++] = t->inner_dia / 2 * v->scale;
    float dot_r = t->solid_dot ? t->ring_dia[9] / 2 * v->scale : 0;
    float lw = (v->scale > 3.0f) ? 1.4f : 1.1f;

    int x0 = 0, y0 = 0, x1 = TGT_SIZE, y1 = TGT_SIZE;
    if (!clip(c, &x0, &y0, &x1, &y1)) return;
    for (int yy = y0; yy < y1; yy++) {
        float dy = yy + 0.5f - TGT_CY;
        uint16_t *row = &c->buf[(yy - c->y0) * c->w];
        for (int xx = x0; xx < x1; xx++) {
            float dx = xx + 0.5f - TGT_CX;
            float d = sqrtf(dx * dx + dy * dy);
            rgb_t col = mix(C_PAPER, C_BLACK, clamp01(black_r - d + 0.5f));
            for (int i = 0; i < nr; i++) {
                float cov = clamp01(lw / 2 + 0.5f - fabsf(d - rr[i]));
                if (cov > 0) col = mix(col, rr[i] < black_r - 0.5f ? C_LINE_LT : C_LINE_DK, cov);
            }
            if (dot_r > 0) col = mix(col, C_RIM, clamp01(dot_r - d + 0.5f));
            row[xx - c->x0] = pack(col);
        }
    }

    // Ring numbers 1..8 on the four axes, where the band is wide enough.
    for (int k = 1; k <= 8; k++) {
        float band = (t->ring_dia[k - 1] - t->ring_dia[k]) / 2 * v->scale;
        if (band < 11) continue;
        float mid = (t->ring_dia[k - 1] + t->ring_dia[k]) / 4 * v->scale;
        if (mid > TGT_SIZE / 2.0f - 6) continue;
        int sc = band >= 26 ? 2 : 1;
        char s[3];
        snprintf(s, sizeof(s), "%d", k);
        rgb_t col = mid < black_r ? C_NUM_LT : C_NUM_DK;
        int tw = text_w(s, sc), th = 7 * sc;
        const float ax[4][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };
        for (int a = 0; a < 4; a++) {
            int px = (int)lroundf(TGT_CX + ax[a][0] * mid - tw / 2.0f);
            int py = (int)lroundf(TGT_CY + ax[a][1] * mid - th / 2.0f);
            text(c, px, py, s, sc, col);
        }
    }
}

static void draw_shots(canvas_t *c, const model_t *m, const view_t *v)
{
    float r = v->hole_r;
    float rim = r < 6 ? 1.0f : 1.5f;
    for (int i = 0; i < m->count; i++) {
        const shot_t *s = &m->shots[i];
        if (s->miss) continue;                                // misses are not drawn (PC app does the same)
        bool last = (i == m->count - 1);
        bool old = (m->count - 1 - i) >= 10;
        float cx = TGT_CX + s->x * v->scale, cy = TGT_CY - s->y * v->scale;
        rgb_t fillc = last ? C_LAST : (old ? C_SHOT_OLD : C_SHOT);
        if (last) ring(c, cx, cy, r + 3.0f, 1.6f, C_LAST, 0.85f);   // halo on the newest
        disc(c, cx, cy, r, C_RIM, 1.0f);
        disc(c, cx, cy, r - rim, fillc, 1.0f);
        char num[6];
        snprintf(num, sizeof(num), "%d", s->number);
        int tw = text_w(num, 1);
        if (r * 2 >= tw + 5 && r >= 6) {
            text(c, (int)lroundf(cx - tw / 2.0f), (int)lroundf(cy - 3.5f), num, 1, C_RIM);
        }
    }

    // Mean group, as the PC app's drawMeanGroup(): from 2 shots, a cross at the
    // group centre and a circle of the mean distance of shots from it. Misses
    // are left out, as in Session.cs.
    int n = 0;
    float sx = 0, sy = 0;
    for (int i = 0; i < m->count; i++) {
        if (m->shots[i].miss) continue;
        sx += m->shots[i].x; sy += m->shots[i].y; n++;
    }
    if (n >= 2) {
        float mx = sx / n, my = sy / n, rsum = 0;
        for (int i = 0; i < m->count; i++) {
            if (m->shots[i].miss) continue;
            float dx = m->shots[i].x - mx, dy = m->shots[i].y - my;
            rsum += sqrtf(dx * dx + dy * dy);
        }
        float cx = TGT_CX + mx * v->scale, cy = TGT_CY - my * v->scale;
        float rbar = rsum / n * v->scale;
        ring(c, cx, cy, rbar, 2.6f, C_BLACK, 0.45f);          // dark under-stroke for contrast
        ring(c, cx, cy, rbar, 1.4f, C_MPI, 1.0f);
        float arm = 5.0f;                                      // PC app: always 5 px
        fill(c, (int)lroundf(cx - arm) - 1, (int)lroundf(cy) - 1, (int)(arm * 2) + 3, 3, C_BLACK, 0.5f);
        fill(c, (int)lroundf(cx) - 1, (int)lroundf(cy - arm) - 1, 3, (int)(arm * 2) + 3, C_BLACK, 0.5f);
        fill(c, (int)lroundf(cx - arm), (int)lroundf(cy), (int)(arm * 2) + 1, 1, C_MPI, 1.0f);
        fill(c, (int)lroundf(cx), (int)lroundf(cy - arm), 1, (int)(arm * 2) + 1, C_MPI, 1.0f);
    }
}

static void chip(canvas_t *c, int x, int y, const char *s, int scale, rgb_t color)
{
    int w = text_w(s, scale) + 8 * scale / 2 + 4, h = 7 * scale + 6;
    rrect(c, x, y, w, h, 3, C_CHIP, 0.62f);
    text(c, x + (w - text_w(s, scale)) / 2, y + 3, s, scale, color);
}

static void center_chip(canvas_t *c, int cy, const char *l1, const char *l2)
{
    int w1 = text_w(l1, 2), w2 = l2 ? text_w(l2, 1) : 0;
    int w = (w1 > w2 ? w1 : w2) + 20, h = l2 ? 40 : 28;
    int x = (TGT_SIZE - w) / 2;
    rrect(c, x, cy, w, h, 5, C_CHIP, 0.72f);
    text(c, (TGT_SIZE - w1) / 2, cy + 7, l1, 2, C_TEXT);
    if (l2) text(c, (TGT_SIZE - w2) / 2, cy + 27, l2, 1, C_LABEL);
}

static void draw_target(canvas_t *c, const model_t *m)
{
    view_t v = compute_view(m);
    draw_face(c, m, &v);
    draw_shots(c, m, &v);

    chip(c, 4, 4, m->target->name, 1, C_TEXT);
    const char *z = m->zoom == ZOOM_AUTO ? "AUTO" : "FULL";
    chip(c, TGT_SIZE - 4 - (text_w(z, 1) + 8), 4, z, 1, C_TEXT);

    // SD card log: shown only when a card is in.
    char sd[24];
    switch (m->sd) {
    case SD_OK:
        snprintf(sd, sizeof(sd), "SD %s", m->sd_file[0] ? m->sd_file : "READY");
        chip(c, 4, TGT_SIZE - 17, sd, 1, C_OK);
        break;
    case SD_NOFS: chip(c, 4, TGT_SIZE - 17, "SD: FORMAT FAT32", 1, C_WAIT); break;
    case SD_ERR:  chip(c, 4, TGT_SIZE - 17, "SD ERROR", 1, C_LAST); break;
    default: break;
    }

    char l2[48];
    switch (m->net) {
    case NET_START:
    case NET_SCAN:
        snprintf(l2, sizeof(l2), "LOOKING FOR %s*", FET_SSID[0] ? FET_SSID : FET_SSID_PREFIX);
        center_chip(c, 168, "SEARCHING", l2);
        break;
    case NET_JOIN:
        center_chip(c, 168, "JOINING", m->ssid);
        break;
    case NET_LINK:
        center_chip(c, 168, "CONNECTING", "TO TARGET");
        break;
    case NET_LIVE:
        if (m->count == 0) center_chip(c, 168, "READY", "WAITING FOR FIRST SHOT");
        break;
    }
    if (m->banner[0]) center_chip(c, 96, m->banner, "HOLD BOOT 1S FOR NEXT");
}

// ---------------------------------------------------------------------------
// Side panel
// ---------------------------------------------------------------------------
#define PX 240
#define PW 80

static void fmt_score(char *out, size_t n, const target_def_t *t, const score_t *s)
{
    if (target_decimal(t)) snprintf(out, n, "%d.%d", s->tenths / 10, s->tenths % 10);
    else snprintf(out, n, "%d", s->ring);
}

static void draw_status(canvas_t *c, const model_t *m, bool blink)
{
    fill(c, PX + 1, 0, PW - 1, UI_STATUS_H, C_PANEL, 1.0f);
    const char *word;
    rgb_t dot;
    switch (m->net) {
    case NET_LIVE: word = "LIVE"; dot = C_OK; break;
    case NET_LINK: word = "LINK"; dot = C_WAIT; break;
    case NET_JOIN: word = "JOIN"; dot = C_WAIT; break;
    case NET_SCAN: word = "SCAN"; dot = C_WAIT; break;
    default:       word = "BOOT"; dot = C_DIM; break;
    }
    bool show = (m->net == NET_LIVE) || blink;
    disc(c, PX + 10.5f, 11.5f, 4.5f, show ? dot : C_DIM, 1.0f);
    text(c, PX + 20, 5, word, 2, m->net == NET_LIVE ? C_TEXT : C_LABEL);

    if (m->net >= NET_LINK) {                                   // signal bars
        int bars = m->rssi > -55 ? 4 : m->rssi > -67 ? 3 : m->rssi > -78 ? 2 : 1;
        for (int i = 0; i < 4; i++) {
            int bh = 3 + i * 3;
            fill(c, PX + 64 + i * 4, 17 - bh, 3, bh, i < bars ? C_TEXT : C_DIM, 1.0f);
        }
    }
}

static void draw_panel(canvas_t *c, const model_t *m, bool blink)
{
    const target_def_t *t = m->target;
    char s[24];

    fill(c, PX, 0, PW, UI_H, C_PANEL, 1.0f);
    fill(c, PX, 0, 1, UI_H, C_RULE, 1.0f);
    draw_status(c, m, blink);

    // network name
    char ssid[14] = "-";
    if (m->ssid[0]) {
        memcpy(ssid, m->ssid, sizeof(ssid) - 1);       // fits 13 chars in the panel
        ssid[sizeof(ssid) - 1] = '\0';
    }
    text(c, PX + 6, 25, ssid, 1, C_LABEL);
    fill(c, PX + 6, 37, PW - 12, 1, C_RULE, 1.0f);

    // last shot, big
    text(c, PX + 6, 43, "LAST", 1, C_LABEL);
    if (m->count > 0) {
        const shot_t *ls = &m->shots[m->count - 1];
        snprintf(s, sizeof(s), "#%d", ls->number);
        text(c, PX + PW - 6 - text_w(s, 1), 43, s, 1, C_LABEL);
        fmt_score(s, sizeof(s), t, &ls->score);
        rgb_t col = ls->score.inner ? C_GOLD : (ls->score.ring == 0 ? C_LAST : C_TEXT);
        float h = 38;
        int w = seg_width(s, h);
        bool x_mark = !target_decimal(t) && ls->score.inner;
        int xw = x_mark ? 14 : 0;
        int x = PX + (PW - w - xw) / 2;
        seg_text(c, (float)x, 56, h, s, col);
        if (x_mark) text(c, x + w + 4, 56, "X", 2, C_GOLD);
        if (ls->miss || ls->score.ring == 0) text(c, PX + (PW - text_w("MISS", 1)) / 2, 97, "MISS", 1, C_LAST);
    } else {
        int w = seg_width("--", 38);
        seg_text(c, (float)(PX + (PW - w) / 2), 56, 38, "--", C_DIM);
    }
    fill(c, PX + 6, 106, PW - 12, 1, C_RULE, 1.0f);

    // running total
    int tenths = 0, rings = 0, xs = 0;
    for (int i = 0; i < m->count; i++) {
        tenths += m->shots[i].score.tenths;
        rings += m->shots[i].score.ring;
        xs += m->shots[i].score.inner ? 1 : 0;
    }
    text(c, PX + 6, 112, "TOTAL", 1, C_LABEL);
    snprintf(s, sizeof(s), "%d SH", m->count);
    text(c, PX + PW - 6 - text_w(s, 1), 112, s, 1, C_LABEL);
    if (target_decimal(t)) snprintf(s, sizeof(s), "%d.%d", tenths / 10, tenths % 10);
    else snprintf(s, sizeof(s), "%d", rings);
    {
        float h = 22;
        int w = seg_width(s, h);
        seg_text(c, (float)(PX + PW - 6 - w), 124, h, s, m->count ? C_TEXT : C_DIM);
        snprintf(s, sizeof(s), "%dX", xs);
        text(c, PX + 6, 139, s, 1, xs ? C_GOLD : C_DIM);
    }
    fill(c, PX + 6, 153, PW - 12, 1, C_RULE, 1.0f);

    // recent shots, newest first
    for (int row = 0; row < 5; row++) {
        int i = m->count - 1 - row;
        if (i < 0) break;
        const shot_t *sh = &m->shots[i];
        int y = 159 + row * 16;
        snprintf(s, sizeof(s), "%d", sh->number);
        if (row == 0) disc(c, PX + 8.5f, y + 7.0f, 2.5f, C_LAST, 1.0f);
        text(c, PX + 14, y + 4, s, 1, C_LABEL);
        fmt_score(s, sizeof(s), t, &sh->score);
        if (!target_decimal(t) && sh->score.inner) strcat(s, "X");
        if (sh->miss) strcpy(s, "MISS");
        rgb_t col = sh->score.inner ? C_GOLD
                  : sh->score.ring >= 10 ? C_TEXT
                  : sh->score.ring >= 9 ? mix(C_TEXT, C_LABEL, 0.4f)
                  : sh->score.ring == 0 ? C_LAST : C_LABEL;
        text(c, PX + PW - 6 - text_w(s, 2), y, s, 2, col);
    }
}

// ---------------------------------------------------------------------------
void ui_render(const model_t *m, bool blink, int x0, int y0, int w, int h, uint16_t *buf)
{
    canvas_t c = { buf, x0, y0, w, h };
    if (x0 < TGT_SIZE) draw_target(&c, m);
    if (x0 + w > PX) {
        if (x0 >= UI_STATUS_X && y0 >= UI_STATUS_Y && y0 + h <= UI_STATUS_Y + UI_STATUS_H) {
            draw_status(&c, m, blink);            // status-only refresh
        } else {
            draw_panel(&c, m, blink);
        }
    }
}
