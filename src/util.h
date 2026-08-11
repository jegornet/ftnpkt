#ifndef FTNPKT_UTIL_H
#define FTNPKT_UTIL_H

#include <stddef.h>
#include <stdint.h>

/*
 * Growable, always-NUL-terminated string buffer. Also used to carry raw byte
 * payloads: logical length is authoritative and embedded NULs are allowed
 * within [0..len); a single trailing '\0' is kept at data[len] for convenience.
 */
typedef struct {
    char    *data;
    size_t   len;
    size_t   cap;
} sbuf;

void  sbuf_init(sbuf *s);
void  sbuf_free(sbuf *s);
void  sbuf_reset(sbuf *s);
int   sbuf_reserve(sbuf *s, size_t extra);            /* 0 ok, -1 oom */
int   sbuf_push(sbuf *s, char c);
int   sbuf_append(sbuf *s, const void *data, size_t n);
int   sbuf_append_cstr(sbuf *s, const char *cstr);
int   sbuf_append_printf(sbuf *s, const char *fmt, ...);

/* Lowercase hex of n bytes appended to s. */
void  hex_lower(const uint8_t *data, size_t n, sbuf *out);

/* n random bytes as lowercase hex (2*n chars). xorshift64 from wall clock+pid. */
void  random_hex(size_t nbytes, sbuf *out);

/* malloc + memcpy; returns NULL on failure. */
void *bytes_dup(const void *src, size_t n);

/*
 * Renders the current local time as a FidoNet DateTime string
 * "dd MMM yy  HH:MM:SS" (two spaces between year and time) appended to s.
 */
int   fido_date_time_now(sbuf *out);

/* Replaces CRLF/LF with a single CR (0x0D). Returns malloc'd string or NULL. */
char *normalize_cr(const char *text);

#endif /* FTNPKT_UTIL_H */
