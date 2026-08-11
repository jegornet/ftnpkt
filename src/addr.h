#ifndef FTNPKT_ADDR_H
#define FTNPKT_ADDR_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint16_t zone;
    uint16_t net;
    uint16_t node;
    uint16_t point;
} ftn_addr_t;

#define FTN_ADDR_ZERO ((ftn_addr_t){0, 0, 0, 0})

/* Parse "Z:N/N[.P]" into out. Returns 0 on success, -1 on error (errmsg). */
int  ftn_parse_addr(const char *s, ftn_addr_t *out, char *errmsg, size_t errsz);

/* Format into a caller buffer (>= FTN_ADDR_BUFSZ bytes). */
#define FTN_ADDR_BUFSZ 32
void ftn_addr2d(ftn_addr_t a, char *buf);   /* "N/N"                  */
void ftn_addr3d(ftn_addr_t a, char *buf);   /* "Z:N/N"                */
void ftn_addr4d(ftn_addr_t a, char *buf);   /* "Z:N/N[.P]"            */

#endif /* FTNPKT_ADDR_H */
