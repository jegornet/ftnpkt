#include "args.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int find_def(const flag_set_t *fs, const char *name)
{
    for (size_t i = 0; i < fs->ndefs; i++)
        if (strcmp(fs->defs[i].name, name) == 0) return (int)i;
    return -1;
}

/* Pointer just past leading dashes, plus how many were consumed. */
static const char *strip_dashes(const char *a, size_t *consumed)
{
    size_t k = 0;
    while (a[k] == '-') k++;
    *consumed = k;
    return a + k;
}

int flag_set_parse(const flag_set_t *fs, int argc, char **argv, parsed_t *out,
                   char *errmsg, size_t errsz)
{
    memset(out, 0, sizeof *out);

    char **ft  = calloc((size_t)(argc > 0 ? argc : 1), sizeof *ft);
    char **pos = calloc((size_t)(argc > 0 ? argc : 1), sizeof *pos);
    if (!ft || !pos) {
        free(ft); free(pos);
        snprintf(errmsg, errsz, "out of memory");
        return -1;
    }
    size_t ft_n = 0, pos_n = 0;

    /* Phase A: split into flag tokens (grabbing values for value flags) and
     * positionals. A bare "-" is a positional (stdin sentinel); "--" ends opts. */
    int i = 0;
    while (i < argc) {
        const char *a = argv[i];
        if (strcmp(a, "--") == 0) {
            for (int k = i + 1; k < argc; k++) pos[pos_n++] = argv[k];
            break;
        }
        if (strcmp(a, "-") == 0) { pos[pos_n++] = argv[i++]; continue; }
        if (a[0] == '-') {
            ft[ft_n++] = argv[i];
            if (!strchr(a, '=')) {
                size_t consumed;
                const char *nm = strip_dashes(a, &consumed);
                char tmp[128];
                snprintf(tmp, sizeof tmp, "%s", nm);
                if (find_def(fs, tmp) >= 0 && i + 1 < argc)
                    ft[ft_n++] = argv[++i];
            }
            i++;
            continue;
        }
        pos[pos_n++] = argv[i++];
    }

    /* Phase B: extract name/value from each flag token. */
    int rc = 0;
    size_t j = 0;
    while (j < ft_n) {
        const char *tok = ft[j];
        const char *eq = strchr(tok, '=');
        char name[128];
        const char *val;
        if (eq) {
            size_t consumed;
            const char *nm = strip_dashes(tok, &consumed);
            size_t nmlen = (size_t)(eq - tok) - consumed;
            if (nmlen >= sizeof name) nmlen = sizeof name - 1;
            memcpy(name, nm, nmlen); name[nmlen] = '\0';
            val = eq + 1;
        } else {
            size_t consumed;
            snprintf(name, sizeof name, "%s", strip_dashes(tok, &consumed));
            val = NULL;
        }
        int idx = find_def(fs, name);
        if (idx < 0) {
            snprintf(errmsg, errsz, "flag provided but not defined: -%s", name);
            rc = -1; break;
        }
        if (!val) {
            j++;
            if (j >= ft_n) {
                snprintf(errmsg, errsz, "flag needs an argument: -%s", name);
                rc = -1; break;
            }
            val = ft[j];
        }
        char **arr = realloc(out->vals[idx], (out->counts[idx] + 1) * sizeof *arr);
        if (!arr) { snprintf(errmsg, errsz, "out of memory"); rc = -1; break; }
        out->vals[idx] = arr;
        out->vals[idx][out->counts[idx]++] = (char *)val;
        out->seen[idx] = 1;
        j++;
    }

    if (rc != 0) {
        for (size_t z = 0; z < fs->ndefs; z++) free(out->vals[z]);
        free(ft); free(pos);
        memset(out, 0, sizeof *out);
        return -1;
    }
    out->positionals = pos;
    out->npos = pos_n;
    free(ft);
    return 0;
}

const char *parsed_get(const parsed_t *p, const flag_set_t *fs, const char *name)
{
    int idx = find_def(fs, name);
    if (idx < 0 || p->counts[idx] == 0) return NULL;
    return p->vals[idx][p->counts[idx] - 1];
}

int parsed_seen(const parsed_t *p, const flag_set_t *fs, const char *name)
{
    int idx = find_def(fs, name);
    return idx >= 0 && p->seen[idx];
}

const char **parsed_get_all(const parsed_t *p, const flag_set_t *fs,
                            const char *name, size_t *count)
{
    *count = 0;
    int idx = find_def(fs, name);
    if (idx < 0 || p->counts[idx] == 0) return NULL;
    *count = p->counts[idx];
    return (const char **)p->vals[idx];
}

void parsed_free(parsed_t *p)
{
    if (!p) return;
    /* vals are pointers into argv, just free the arrays. */
    for (size_t z = 0; z < sizeof p->vals / sizeof p->vals[0]; z++) {
        free(p->vals[z]); p->vals[z] = NULL; p->counts[z] = 0;
    }
    free(p->positionals);
    p->positionals = NULL; p->npos = 0;
}

void flag_set_usage(const flag_set_t *fs, sbuf *out)
{
    sbuf_append_cstr(out, fs->usage_intro);
    sbuf_push(out, '\n');
    for (size_t i = 0; i < fs->ndefs; i++) {
        sbuf_append_printf(out, "  -%s\t%s\n", fs->defs[i].name, fs->defs[i].help);
    }
}

int parse_u16_base0(const char *s, uint16_t *out)
{
    if (!s || !*s) return -1;
    char *end = NULL;
    char buf[64];
    snprintf(buf, sizeof buf, "%s", s);
    /* trim leading/trailing whitespace */
    size_t n = strlen(buf);
    while (n && isspace((unsigned char)buf[0])) { memmove(buf, buf + 1, --n); }
    while (n && isspace((unsigned char)buf[n - 1])) buf[--n] = '\0';
    if (n == 0) return -1;
    int base = 10;
    const char *digits = buf;
    if (buf[0] == '0' && (buf[1] == 'x' || buf[1] == 'X')) { base = 16; digits = buf + 2; }
    else if (buf[0] == '0' && (buf[1] == 'o' || buf[1] == 'O')) { base = 8; digits = buf + 2; }
    else if (buf[0] == '0' && (buf[1] == 'b' || buf[1] == 'B')) { base = 2; digits = buf + 2; }
    if (!*digits) return -1;
    long v = strtol(digits, &end, base);
    if (!end || *end != '\0' || v < 0 || v > 65535) return -1;
    *out = (uint16_t)v;
    return 0;
}
