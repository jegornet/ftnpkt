#include "util.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static const char *const MONTHS[12] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
};

void sbuf_init(sbuf *s) { s->data = NULL; s->len = 0; s->cap = 0; }

void sbuf_free(sbuf *s) { free(s->data); s->data = NULL; s->len = s->cap = 0; }

void sbuf_reset(sbuf *s) { s->len = 0; if (s->data) s->data[0] = '\0'; }

int sbuf_reserve(sbuf *s, size_t extra)
{
    size_t need = s->len + extra + 1;            /* +1 for trailing NUL */
    if (need <= s->cap) return 0;
    size_t cap = s->cap ? s->cap : 16;
    while (cap < need) {
        if (cap > (size_t)-1 / 2) return -1;     /* overflow guard */
        cap *= 2;
    }
    char *p = realloc(s->data, cap);
    if (!p) return -1;
    s->data = p;
    s->cap  = cap;
    if (s->len == 0) s->data[0] = '\0';
    return 0;
}

int sbuf_push(sbuf *s, char c)
{
    if (sbuf_reserve(s, 1) != 0) return -1;
    s->data[s->len++] = c;
    s->data[s->len]   = '\0';
    return 0;
}

int sbuf_append(sbuf *s, const void *data, size_t n)
{
    if (n == 0) return 0;
    if (sbuf_reserve(s, n) != 0) return -1;
    memcpy(s->data + s->len, data, n);
    s->len += n;
    s->data[s->len] = '\0';
    return 0;
}

int sbuf_append_cstr(sbuf *s, const char *cstr)
{
    return sbuf_append(s, cstr, strlen(cstr));
}

int sbuf_append_printf(sbuf *s, const char *fmt, ...)
{
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0) { va_end(ap2); return -1; }
    if (sbuf_reserve(s, (size_t)n) != 0) { va_end(ap2); return -1; }
    vsnprintf(s->data + s->len, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    s->len += (size_t)n;
    return 0;
}

void hex_lower(const uint8_t *data, size_t n, sbuf *out)
{
    static const char HEX[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        sbuf_push(out, HEX[(data[i] >> 4) & 0x0f]);
        sbuf_push(out, HEX[data[i] & 0x0f]);
    }
}

/* xorshift64 seeded from wall-clock nanos and PID. */
static uint64_t rng_seed(void)
{
    struct timespec ts;
    uint64_t nanos = 0;
    if (clock_gettime(CLOCK_REALTIME, &ts) == 0)
        nanos = (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
    uint64_t pid = (uint64_t)getpid();
    uint64_t state = nanos ^ (pid << 17) ^ 0x9E3779B97F4A7C15ull;
    return state ? state : 0xDEADBEEFull;
}

void random_hex(size_t nbytes, sbuf *out)
{
    uint64_t state = rng_seed();
    for (size_t i = 0; i < nbytes; i++) {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        uint8_t b = (uint8_t)(state & 0xff);
        static const char HEX[] = "0123456789abcdef";
        sbuf_push(out, HEX[(b >> 4) & 0x0f]);
        sbuf_push(out, HEX[b & 0x0f]);
    }
}

void *bytes_dup(const void *src, size_t n)
{
    void *p = malloc(n ? n : 1);
    if (p && n) memcpy(p, src, n);
    return p;
}

int fido_date_time_now(sbuf *out)
{
    time_t now = time(NULL);
    struct tm tm;
    if (!localtime_r(&now, &tm)) return -1;
    const char *mon = (tm.tm_mon >= 0 && tm.tm_mon < 12) ? MONTHS[tm.tm_mon] : "???";
    return sbuf_append_printf(out, "%02d %s %02d  %02d:%02d:%02d",
                              tm.tm_mday, mon, tm.tm_year % 100,
                              tm.tm_hour, tm.tm_min, tm.tm_sec);
}

char *normalize_cr(const char *text)
{
    size_t cap = strlen(text) + 1;
    char *out = malloc(cap);
    if (!out) return NULL;
    size_t j = 0;
    for (size_t i = 0; text[i]; i++) {
        if (text[i] == '\r') {
            out[j++] = '\r';
            if (text[i + 1] == '\n') i++;        /* collapse CRLF */
        } else if (text[i] == '\n') {
            out[j++] = '\r';
        } else {
            out[j++] = text[i];
        }
    }
    out[j] = '\0';
    return out;
}
