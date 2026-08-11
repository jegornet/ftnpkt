#include "addr.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static int is_digits(const char *s, size_t n)
{
    if (n == 0) return 0;
    for (size_t i = 0; i < n; i++)
        if (!isdigit((unsigned char)s[i])) return 0;
    return 1;
}

static uint16_t parse_u16_sat(const char *s, size_t n)
{
    unsigned long v = 0;
    for (size_t i = 0; i < n; i++) {
        v = v * 10 + (unsigned long)(s[i] - '0');
        if (v > 65535) v = 65535;
    }
    return (uint16_t)v;
}

static void skip_ws(const char *s, size_t *i, size_t len)
{
    while (*i < len && isspace((unsigned char)s[*i])) (*i)++;
}

int ftn_parse_addr(const char *s, ftn_addr_t *out, char *errmsg, size_t errsz)
{
    out->zone = out->net = out->node = out->point = 0;
    size_t len = strlen(s), i = 0;
    skip_ws(s, &i, len);

    const char *colon = strchr(s + i, ':');
    const char *slash = strchr(s + i, '/');
    if (!colon || !slash || slash <= colon) goto bad;

    size_t zone_lo = i;
    size_t zone_hi = (size_t)(colon - s);
    size_t net_lo  = zone_hi + 1;
    size_t net_hi  = (size_t)(slash - s);

    const char *tail = slash + 1;
    const char *dot  = strchr(tail, '.');
    size_t node_lo = (size_t)(tail - s);
    size_t node_hi = dot ? (size_t)(dot - s) : len;
    int has_point = dot != NULL;
    size_t pt_lo = has_point ? node_hi + 1 : 0;
    size_t pt_hi = len;

    /* reject trailing whitespace only by checking the last non-ws isn't before */
    if (!is_digits(s + zone_lo, zone_hi - zone_lo)) goto bad;
    if (!is_digits(s + net_lo,  net_hi  - net_lo))  goto bad;
    if (!is_digits(s + node_lo, node_hi - node_lo)) goto bad;
    if (has_point && !is_digits(s + pt_lo, pt_hi - pt_lo)) goto bad;

    out->zone  = parse_u16_sat(s + zone_lo, zone_hi - zone_lo);
    out->net   = parse_u16_sat(s + net_lo,  net_hi  - net_lo);
    out->node  = parse_u16_sat(s + node_lo, node_hi - node_lo);
    out->point = has_point ? parse_u16_sat(s + pt_lo, pt_hi - pt_lo) : 0;
    return 0;

bad:
    snprintf(errmsg, errsz,
             "invalid address \"%s\" (expected Z:N/N[.P], e.g. 2:382/736.1)", s);
    return -1;
}

void ftn_addr2d(ftn_addr_t a, char *buf)
{
    snprintf(buf, FTN_ADDR_BUFSZ, "%u/%u", a.net, a.node);
}

void ftn_addr3d(ftn_addr_t a, char *buf)
{
    snprintf(buf, FTN_ADDR_BUFSZ, "%u:%u/%u", a.zone, a.net, a.node);
}

void ftn_addr4d(ftn_addr_t a, char *buf)
{
    if (a.point != 0)
        snprintf(buf, FTN_ADDR_BUFSZ, "%u:%u/%u.%u", a.zone, a.net, a.node, a.point);
    else
        ftn_addr3d(a, buf);
}
