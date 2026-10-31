#include "parse.h"

#include <stdlib.h>
#include <string.h>

void parser_reset(parser_t *p)
{
    p->len = 0;
}

// Find `"key"` followed by optional spaces and ':' inside [s, end). Returns a
// pointer just past the ':' or NULL.
static const char *find_key(const char *s, const char *end, const char *key)
{
    size_t kl = strlen(key);
    for (const char *q = s; q + kl + 2 <= end; q++) {
        if (q[0] == '"' && memcmp(q + 1, key, kl) == 0 && q[kl + 1] == '"') {
            const char *v = q + kl + 2;
            while (v < end && (*v == ' ' || *v == '\t')) v++;
            if (v < end && *v == ':') {
                v++;
                while (v < end && (*v == ' ' || *v == '\t')) v++;
                return v;
            }
        }
    }
    return NULL;
}

static bool get_num(const char *s, const char *end, const char *key, double *out)
{
    const char *v = find_key(s, end, key);
    if (!v) return false;
    char tmp[32];
    size_t n = 0;
    while (v < end && n < sizeof(tmp) - 1 && strchr("+-.0123456789eE", *v)) tmp[n++] = *v++;
    tmp[n] = '\0';
    if (n == 0) return false;
    char *e;
    *out = strtod(tmp, &e);
    return e != tmp;
}

static void get_str(const char *s, const char *end, const char *key, char *out, size_t cap)
{
    out[0] = '\0';
    const char *v = find_key(s, end, key);
    if (!v || v >= end || *v != '"') return;
    v++;
    size_t n = 0;
    while (v < end && *v != '"' && n < cap - 1) out[n++] = *v++;
    out[n] = '\0';
}

static bool parse_one(const char *s, const char *end, parsed_shot_t *sh)
{
    double d;
    memset(sh, 0, sizeof(*sh));
    if (!get_num(s, end, "shot", &d)) return false;
    sh->number = (int)d;
    if (get_num(s, end, "miss", &d)) sh->miss = (d != 0);
    double x, y;
    if (get_num(s, end, "x", &x) && get_num(s, end, "y", &y)) {
        sh->has_xy = true;
        sh->x = x;
        sh->y = y;
    }
    get_str(s, end, "target_name", sh->target_name, sizeof(sh->target_name));
    return true;
}

void parser_feed(parser_t *p, const char *data, size_t len, shot_cb_t cb, void *ctx)
{
    while (len > 0) {
        size_t room = sizeof(p->buf) - p->len;
        size_t n = len < room ? len : room;
        memcpy(p->buf + p->len, data, n);
        p->len += n;
        data += n;
        len -= n;

        for (;;) {
            const char *b = p->buf, *end = p->buf + p->len;
            const char *k = NULL;
            for (const char *q = b; q + 6 <= end; q++) {
                if (memcmp(q, "\"shot\"", 6) == 0) { k = q; break; }
            }
            if (!k) {
                // Nothing yet. Keep a short tail in case "shot" is split.
                if (p->len > 8) {
                    memmove(p->buf, end - 8, 8);
                    p->len = 8;
                }
                break;
            }
            const char *close = memchr(k, '}', (size_t)(end - k));
            if (!close) {
                if (k > b) {                       // drop what is before the key
                    memmove(p->buf, k, (size_t)(end - k));
                    p->len = (size_t)(end - k);
                }
                if (p->len == sizeof(p->buf)) {    // runaway message: give up on it
                    p->len = 0;
                }
                break;
            }
            parsed_shot_t sh;
            if (parse_one(k, close, &sh)) {
                cb(&sh, ctx);
            }
            size_t used = (size_t)(close + 1 - b);
            memmove(p->buf, close + 1, p->len - used);
            p->len -= used;
        }
    }
}
