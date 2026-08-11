#ifndef FTNPKT_ARGS_H
#define FTNPKT_ARGS_H

#include <stddef.h>
#include <stdint.h>

#include "util.h"

typedef struct { const char *name; const char *help; } flag_def_t;

typedef struct {
    const char       *name;        /* command name, e.g. "ftnpkt create" */
    const char       *usage_intro;
    const flag_def_t *defs;
    size_t            ndefs;
} flag_set_t;

typedef struct {
    char  **vals[64];     /* vals[i] -> values for defs[i] (pointers into argv) */
    size_t  counts[64];
    int     seen[64];
    char  **positionals;  /* pointers into argv */
    size_t  npos;
} parsed_t;

/* Parse argv (interspersed flags/positionals). Returns 0 ok, -1 on error. */
int  flag_set_parse(const flag_set_t *fs, int argc, char **argv, parsed_t *out,
                    char *errmsg, size_t errsz);

/* Last value for a single-valued flag, or NULL if absent. */
const char *parsed_get(const parsed_t *p, const flag_set_t *fs, const char *name);

/* All values for a (possibly repeatable) flag; *count set. NULL if absent. */
const char **parsed_get_all(const parsed_t *p, const flag_set_t *fs,
                            const char *name, size_t *count);

/* True if the flag was present on the command line. */
int parsed_seen(const parsed_t *p, const flag_set_t *fs, const char *name);

void parsed_free(parsed_t *p);

/* Render the usage block (intro + per-flag lines) appended to out. */
void flag_set_usage(const flag_set_t *fs, sbuf *out);

/* Parse an integer in base 0 (0x.., 0o.., 0b.., decimal) into a u16. */
int  parse_u16_base0(const char *s, uint16_t *out);

#endif /* FTNPKT_ARGS_H */
