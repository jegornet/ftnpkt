#include "charset.h"

#include <errno.h>
#include <iconv.h>
#include <string.h>

/* Match key -> canonical iconv name. All matching is case-insensitive. */
struct alias { const char *key; const char *canon; };

static int eq_ci(const char *a, const char *b)
{
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
        if (ca != cb) return 0;
        a++; b++;
    }
    return *a == '\0' && *b == '\0';
}

const char *charset_canonical(const char *name)
{
    static const struct alias T[] = {
        { "utf-8",      "UTF-8" },
        { "utf8",       "UTF-8" },
        { "cp437",      "CP437" },     { "ibm437", "CP437" }, { "437", "CP437" },
        { "cp850",      "CP850" },     { "ibm850", "CP850" }, { "850", "CP850" },
        { "cp852",      "CP852" },     { "ibm852", "CP852" },
        { "cp866",      "CP866" },     { "ibm866", "CP866" }, { "866", "CP866" },
        { "cp1250",     "CP1250" },    { "windows-1250", "CP1250" },
        { "cp1251",     "CP1251" },    { "windows-1251", "CP1251" },
        { "cp1252",     "CP1252" },    { "windows-1252", "CP1252" },
        { "cp1253",     "CP1253" },    { "windows-1253", "CP1253" },
        { "cp1257",     "CP1257" },    { "windows-1257", "CP1257" },
        { "koi8-r",     "KOI8-R" },    { "koi8r",  "KOI8-R" },
        { "koi8-u",     "KOI8-U" },    { "koi8u",  "KOI8-U" },
        { "iso-8859-1", "ISO-8859-1" },{ "iso8859-1", "ISO-8859-1" },
        { "latin-1",    "ISO-8859-1" },{ "latin1",    "ISO-8859-1" },
        { "iso-8859-2", "ISO-8859-2" },{ "iso8859-2", "ISO-8859-2" },
        { "latin-2",    "ISO-8859-2" },
        { "iso-8859-5", "ISO-8859-5" },{ "iso8859-5", "ISO-8859-5" },
        { "iso-8859-15","ISO-8859-15"},{ "iso8859-15","ISO-8859-15" },
        { "macintosh",  "MACINTOSH" }, { "mac-roman", "MACINTOSH" },
    };
    if (!name || !*name) return NULL;
    for (size_t i = 0; i < sizeof T / sizeof T[0]; i++)
        if (eq_ci(name, T[i].key)) return T[i].canon;
    return NULL;
}

int charset_is_known(const char *name) { return charset_canonical(name) != NULL; }

/* Byte length of the UTF-8 rune starting at b. */
static size_t utf8_rune_len(unsigned char b)
{
    if (b < 0x80)       return 1;
    if ((b & 0xE0)==0xC0) return 2;
    if ((b & 0xF0)==0xE0) return 3;
    if ((b & 0xF8)==0xF0) return 4;
    return 1;                         /* stray continuation byte */
}

/*
 * Drive an iconv descriptor over [in,in+inlen), appending converted bytes to
 * `out`. On an unconvertable sequence emit `repl` and advance; when skip_rune
 * is set, advance past a whole UTF-8 rune (so one bad rune -> one '?'), else
 * advance a single byte.
 */
static int iconv_run(iconv_t cd, const char *in, size_t inlen, sbuf *out,
                     const char *repl, size_t repl_len, int skip_rune)
{
    char  stage[256];
    char *inp  = (char *)in;
    size_t left = inlen;
    while (left > 0) {
        char  *ostage = stage;
        size_t oleft  = sizeof stage;
        size_t r = iconv(cd, &inp, &left, &ostage, &oleft);
        size_t produced = sizeof stage - oleft;
        if (produced && sbuf_append(out, stage, produced) != 0) return -1;
        if (r == (size_t)-1) {
            if (errno == E2BIG) continue;             /* output full, retry */
            if (sbuf_append(out, repl, repl_len) != 0) return -1;
            size_t adv = skip_rune ? utf8_rune_len((unsigned char)*inp) : 1;
            if (adv > left) adv = left;
            inp  += adv;
            left -= adv;
        }
    }
    /* Flush any pending shift state (no-op for the stateless code pages here). */
    for (;;) {
        char  *ostage = stage;
        size_t oleft  = sizeof stage;
        size_t r = iconv(cd, NULL, NULL, &ostage, &oleft);
        size_t produced = sizeof stage - oleft;
        if (produced && sbuf_append(out, stage, produced) != 0) return -1;
        if (r != (size_t)-1 || errno != E2BIG) break;
    }
    return 0;
}

int charset_encode(const char *name, const char *utf8, sbuf *out)
{
    const char *canon = charset_canonical(name);
    if (!canon) return -1;
    if (strcmp(canon, "UTF-8") == 0) return sbuf_append_cstr(out, utf8);
    iconv_t cd = iconv_open(canon, "UTF-8");
    if (cd == (iconv_t)-1) return -1;
    int rc = iconv_run(cd, utf8, strlen(utf8), out, "?", 1, 1);
    iconv_close(cd);
    return rc;
}

int charset_decode(const char *name, const uint8_t *data, size_t len, sbuf *out)
{
    const char *canon = charset_canonical(name);
    if (!canon) return -1;
    if (strcmp(canon, "UTF-8") == 0) return sbuf_append(out, data, len);
    iconv_t cd = iconv_open("UTF-8", canon);
    if (cd == (iconv_t)-1) return -1;
    static const char REPL[] = "\xEF\xBF\xBD";       /* U+FFFD */
    int rc = iconv_run(cd, (const char *)data, len, out, REPL, 3, 0);
    iconv_close(cd);
    return rc;
}
