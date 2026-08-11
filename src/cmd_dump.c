#include "commands.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "args.h"
#include "charset.h"
#include "packet.h"
#include "util.h"

static const flag_def_t DEFS[] = {
    { "ctrl",  "notation for control bytes (0..31, 127): caret (^A, ^M, ...) or unicode Control Pictures (U+2400..U+2421)" },
    { "color", "highlight control characters with ANSI dim: auto detects a TTY (default), always forces it on, never turns it off" },
    { "chrs",  "force the input charset of message text (e.g. cp866); overrides the ^ACHRS kludge" },
};

static const flag_set_t FS = {
    .name = "ftnpkt dump",
    .usage_intro = "Usage: ftnpkt dump <file.pkt> [options]",
    .defs = DEFS, .ndefs = sizeof DEFS / sizeof DEFS[0],
};

#define DIM   "\x1b[2m"
#define RESET "\x1b[0m"

/* ---- low-level rendering ---- */
static void emit_cp(sbuf *o, uint32_t cp)
{
    if (cp < 0x80) { sbuf_push(o, (char)cp); }
    else if (cp < 0x800) {
        sbuf_push(o, (char)(0xC0 | (cp >> 6)));
        sbuf_push(o, (char)(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        sbuf_push(o, (char)(0xE0 | (cp >> 12)));
        sbuf_push(o, (char)(0x80 | ((cp >> 6) & 0x3F)));
        sbuf_push(o, (char)(0x80 | (cp & 0x3F)));
    } else {
        sbuf_push(o, (char)(0xF0 | (cp >> 18)));
        sbuf_push(o, (char)(0x80 | ((cp >> 12) & 0x3F)));
        sbuf_push(o, (char)(0x80 | ((cp >> 6) & 0x3F)));
        sbuf_push(o, (char)(0x80 | (cp & 0x3F)));
    }
}

static void control_token_text(uint8_t b, const char *notation, sbuf *o)
{
    if (strcmp(notation, "unicode") == 0) {
        emit_cp(o, b == 127 ? 0x2421 : 0x2400u + b);
        return;
    }
    if (b == 127) sbuf_append_cstr(o, "^?");
    else { sbuf_push(o, '^'); sbuf_push(o, (char)(b + 64)); }
}

static void emit_control(sbuf *o, uint8_t b, const char *notation, int color)
{
    if (color) sbuf_append_cstr(o, DIM);
    control_token_text(b, notation, o);
    if (color) sbuf_append_cstr(o, RESET);
}

static void display_bytes(const uint8_t *d, size_t n, int color, const char *notation, sbuf *o)
{
    if (n == 0) { sbuf_append_cstr(o, "(empty)"); return; }
    for (size_t i = 0; i < n; i++) {
        uint8_t b = d[i];
        if (b <= 31 || b == 127) emit_control(o, b, notation, color);
        else emit_cp(o, b);                      /* Latin-1 codepoint == byte */
    }
}

static void display_string(const char *utf8, size_t n, int color, const char *notation, sbuf *o)
{
    if (n == 0) { sbuf_append_cstr(o, "(empty)"); return; }
    const uint8_t *d = (const uint8_t *)utf8;
    size_t i = 0;
    while (i < n) {
        uint8_t b = d[i];
        uint32_t cp; size_t adv;
        if (b < 0x80) { cp = b; adv = 1; }
        else if ((b & 0xE0) == 0xC0 && i + 1 < n) {
            cp = ((uint32_t)(b & 0x1F) << 6) | (d[i + 1] & 0x3F); adv = 2;
        } else if ((b & 0xF0) == 0xE0 && i + 2 < n) {
            cp = ((uint32_t)(b & 0x0F) << 12) | ((uint32_t)(d[i + 1] & 0x3F) << 6) | (d[i + 2] & 0x3F);
            adv = 3;
        } else if ((b & 0xF8) == 0xF0 && i + 3 < n) {
            cp = ((uint32_t)(b & 0x07) << 18) | ((uint32_t)(d[i + 1] & 0x3F) << 12)
               | ((uint32_t)(d[i + 2] & 0x3F) << 6) | (d[i + 3] & 0x3F);
            adv = 4;
        } else { cp = 0xFFFD; adv = 1; }
        if (cp <= 31 || cp == 127) emit_control(o, (uint8_t)cp, notation, color);
        else emit_cp(o, cp);
        i += adv;
    }
}

static void display_field(const uint8_t *d, size_t n, const char *chrs,
                          int color, const char *notation, sbuf *o)
{
    if (n == 0) { sbuf_append_cstr(o, "(empty)"); return; }
    sbuf dec; sbuf_init(&dec);
    if (chrs && charset_decode(chrs, d, n, &dec) == 0)
        display_string(dec.data, dec.len, color, notation, o);
    else
        display_bytes(d, n, color, notation, o);
    sbuf_free(&dec);
}

/* ---- charset resolution from the CHRS kludge ---- */
static char *extract_chrs(const uint8_t *text, size_t len)
{
    size_t i = 0;
    while (i < len) {
        size_t j = i;
        while (j < len && text[j] != 0x0D) j++;     /* one CR-delimited line */
        size_t line_lo = i, line_hi = j;
        i = (j < len) ? j + 1 : len;
        /* trim trailing spaces/tabs */
        while (line_hi > line_lo && (text[line_hi - 1] == ' ' || text[line_hi - 1] == '\t')) line_hi--;
        if (line_hi - line_lo < 2 || text[line_lo] != 0x01) continue;
        size_t rest_lo = line_lo + 1, rest_hi = line_hi;
        /* case-insensitive prefix test for CHRS:/CHARSET: */
        static const char *const KW[2] = { "CHRS:", "CHARSET:" };
        for (int k = 0; k < 2; k++) {
            const char *kw = KW[k];
            size_t kl = strlen(kw);
            if (rest_hi - rest_lo < kl) continue;
            int ok = 1;
            for (size_t x = 0; x < kl; x++) {
                char c = (char)text[rest_lo + x];
                if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
                if (c != kw[x]) { ok = 0; break; }
            }
            if (!ok) continue;
            /* first whitespace-delimited token after the keyword */
            size_t t = rest_lo + kl;
            while (t < rest_hi && (text[t] == ' ' || text[t] == '\t')) t++;   /* trim left */
            size_t e = t;
            while (e < rest_hi && text[e] != ' ' && text[e] != '\t') e++;
            return strndup((const char *)(text + t), e - t);
        }
    }
    char *e = malloc(1); if (e) e[0] = '\0'; return e;
}

static char *resolve_charset(const char *forced, const uint8_t *text, size_t len)
{
    if (forced && forced[0]) return strdup(forced);
    return extract_chrs(text, len);
}

static void hexdump(const uint8_t *d, size_t n, sbuf *o)
{
    sbuf_append_cstr(o, "0x");
    hex_lower(d, n, o);
}

/* ---- attribute names ---- */
static void format_attributes(const packed_msg_t *m, sbuf *o)
{
    const struct { uint16_t bit; const char *name; } T[] = {
        { ATTR_PRIVATE,            "Private" },         { ATTR_CRASH,           "Crash" },
        { ATTR_RECD,               "Recd" },            { ATTR_SENT,            "Sent" },
        { ATTR_FILE_ATTACHED,      "FileAttached" },    { ATTR_IN_TRANSIT,      "InTransit" },
        { ATTR_ORPHAN,             "Orphan" },          { ATTR_KILL_SENT,       "KillSent" },
        { ATTR_LOCAL,              "Local" },           { ATTR_HOLD_FOR_PICKUP, "HoldForPickup" },
        { ATTR_FILE_REQUEST,       "FileRequest" },     { ATTR_RETURN_RECEIPT_REQ, "ReturnReceiptReq" },
        { ATTR_RETURN_RECEIPT,     "ReturnReceipt" },   { ATTR_AUDIT_REQUEST,   "AuditRequest" },
        { ATTR_FILE_UPDATE_REQ,    "FileUpdateReq" },
    };
    int first = 1;
    for (size_t i = 0; i < sizeof T / sizeof T[0]; i++) {
        if (!pm_has(m, T[i].bit)) continue;
        sbuf_append_cstr(o, first ? " (" : ", ");
        sbuf_append_cstr(o, T[i].name);
        first = 0;
    }
    if (!first) sbuf_push(o, ')');
}

/* ---- packet dump ---- */
static void common_fields(const packet_t *p, int color, const char *notation, sbuf *o)
{
    sbuf_append_printf(o, "  origNode:     %u\n", p->orig_node);
    sbuf_append_printf(o, "  destNode:     %u\n", p->dest_node);
    sbuf_append_printf(o, "  year:         %u\n", p->year);
    sbuf_append_printf(o, "  month:        %u\n", p->month);
    sbuf_append_printf(o, "  day:          %u\n", p->day);
    sbuf_append_printf(o, "  hour:         %u\n", p->hour);
    sbuf_append_printf(o, "  minute:       %u\n", p->minute);
    sbuf_append_printf(o, "  second:       %u\n", p->second);
    sbuf_append_printf(o, "  baud:         %u\n", p->baud);
    sbuf_append_printf(o, "  origNet:      %u\n", p->orig_net);
    sbuf_append_printf(o, "  destNet:      %u\n", p->dest_net);
    sbuf_append_printf(o, "  origZone:     %u\n", p->orig_zone);
    sbuf_append_printf(o, "  destZone:     %u\n", p->dest_zone);
    sbuf_append_printf(o, "  prodCodeLo:   0x%02x\n", p->prod_code_lo);
    sbuf_append_printf(o, "  prodRevMajor: %u\n", p->prod_rev_major);
    sbuf_append_cstr(o, "  password:     ");
    display_bytes(p->password, FTN_PASSWORD_SIZE, color, notation, o);
    sbuf_push(o, '\n');
}

static void dump_packet(const packet_t *p, int color, const char *notation,
                        const char *forced_chrs, sbuf *o)
{
    if (p->type == PKT_TYPE2PLUS) {
        sbuf_append_cstr(o, "Packet type: Type-2+ (FSC-0048)\n");
        common_fields(p, color, notation, o);
        sbuf_append_printf(o, "  qOrigZone:    %u\n", p->q_orig_zone);
        sbuf_append_printf(o, "  qDestZone:    %u\n", p->q_dest_zone);
        sbuf_append_printf(o, "  auxNet:       %u\n", p->aux_net);
        sbuf_append_printf(o, "  capValid:     0x%x\n", p->cap_valid);
        sbuf_append_printf(o, "  prodCodeHi:   0x%02x\n", p->prod_code_hi);
        sbuf_append_printf(o, "  prodRevMinor: %u\n", p->prod_rev_minor);
        sbuf_append_printf(o, "  capWord:      0x%x\n", p->cap_word);
        sbuf_append_printf(o, "  origPoint:    %u\n", p->orig_point);
        sbuf_append_printf(o, "  destPoint:    %u\n", p->dest_point);
        sbuf_append_cstr(o, "  prodData:     "); hexdump(p->prod_data, FTN_PROD_DATA_SIZE, o);
        sbuf_push(o, '\n');
    } else if (p->type == PKT_TYPE2E) {
        sbuf_append_cstr(o, "Packet type: Type-2e (FSC-0039)\n");
        common_fields(p, color, notation, o);
        sbuf_append_printf(o, "  qOrigZone:    %u\n", p->q_orig_zone);
        sbuf_append_printf(o, "  qDestZone:    %u\n", p->q_dest_zone);
        sbuf_append_printf(o, "  filler:       0x%x\n", p->aux_net);
        sbuf_append_printf(o, "  capValid:     0x%x\n", p->cap_valid);
        sbuf_append_printf(o, "  prodCodeHi:   0x%02x\n", p->prod_code_hi);
        sbuf_append_printf(o, "  prodRevMinor: %u\n", p->prod_rev_minor);
        sbuf_append_printf(o, "  capWord:      0x%x\n", p->cap_word);
        sbuf_append_printf(o, "  origPoint:    %u\n", p->orig_point);
        sbuf_append_printf(o, "  destPoint:    %u\n", p->dest_point);
        sbuf_append_cstr(o, "  prodData:     "); hexdump(p->prod_data, FTN_PROD_DATA_SIZE, o);
        sbuf_push(o, '\n');
    } else {
        sbuf_append_cstr(o, "Packet type: Type-2 Stone Age (FTS-0001)\n");
        common_fields(p, color, notation, o);
        sbuf_append_cstr(o, "  fill:         ");
        display_bytes(p->fill, FTN_FILL_SIZE, color, notation, o);
        sbuf_push(o, '\n');
    }

    sbuf_push(o, '\n');
    sbuf_append_printf(o, "Messages: %zu\n", p->msg_count);

    for (size_t i = 0; i < p->msg_count; i++) {
        const packed_msg_t *m = &p->messages[i];
        sbuf_push(o, '\n');
        sbuf_append_printf(o, "--- Message #%zu ---\n", i + 1);
        sbuf_append_printf(o, "  origNode:     %u\n", m->orig_node);
        sbuf_append_printf(o, "  destNode:     %u\n", m->dest_node);
        sbuf_append_printf(o, "  origNet:      %u\n", m->orig_net);
        sbuf_append_printf(o, "  destNet:      %u\n", m->dest_net);
        sbuf_append_printf(o, "  attribute:    0x%x", m->attribute);
        sbuf tmp; sbuf_init(&tmp);
        format_attributes(m, &tmp);
        sbuf_append_cstr(o, tmp.data ? tmp.data : "");
        sbuf_free(&tmp);
        sbuf_push(o, '\n');
        sbuf_append_printf(o, "  cost:         %u\n", m->cost);

        char *chrs = resolve_charset(forced_chrs, m->text, m->text_len);
        sbuf_append_cstr(o, "  dateTime:     ");
        display_bytes(m->date_time, m->date_time_len, color, notation, o);
        sbuf_push(o, '\n');
        sbuf_append_cstr(o, "  toUserName:   ");
        display_field(m->to_user_name, m->to_user_name_len, chrs, color, notation, o);
        sbuf_push(o, '\n');
        sbuf_append_cstr(o, "  fromUserName: ");
        display_field(m->from_user_name, m->from_user_name_len, chrs, color, notation, o);
        sbuf_push(o, '\n');
        sbuf_append_cstr(o, "  subject:      ");
        display_field(m->subject, m->subject_len, chrs, color, notation, o);
        sbuf_push(o, '\n');
        sbuf_append_cstr(o, "  text:         ");
        display_field(m->text, m->text_len, chrs, color, notation, o);
        sbuf_push(o, '\n');
        free(chrs);
    }
}

int cmd_dump(int argc, char **argv)
{
    parsed_t p;
    char err[256];
    sbuf usage; sbuf_init(&usage);

    if (flag_set_parse(&FS, argc, argv, &p, err, sizeof err) != 0) {
        fprintf(stderr, "ftnpkt dump: %s\n", err);
        flag_set_usage(&FS, &usage);
        fprintf(stderr, "%s", usage.data ? usage.data : "");
        sbuf_free(&usage);
        return 2;
    }
    if (p.npos != 1) {
        flag_set_usage(&FS, &usage);
        fprintf(stderr, "%s", usage.data ? usage.data : "");
        sbuf_free(&usage);
        parsed_free(&p);
        return 2;
    }
    sbuf_free(&usage);
    const char *path = p.positionals[0];

    const char *ctrl  = parsed_get(&p, &FS, "ctrl");
    const char *color = parsed_get(&p, &FS, "color");
    const char *chrs  = parsed_get(&p, &FS, "chrs");
    char ctrl_buf[16], color_buf[16];
    snprintf(ctrl_buf,  sizeof ctrl_buf,  "%s", ctrl  ? ctrl  : "caret");
    snprintf(color_buf, sizeof color_buf, "%s", color ? color : "auto");
    const char *notation = ctrl_buf;
    const char *cmode    = color_buf;
    const char *fchrs    = chrs ? chrs : "";

    if (strcmp(notation, "caret") != 0 && strcmp(notation, "unicode") != 0) {
        fprintf(stderr, "ftnpkt dump: invalid --ctrl \"%s\" (expected caret or unicode)\n", notation);
        parsed_free(&p);
        return 2;
    }
    if (fchrs[0] && !charset_is_known(fchrs))
        fprintf(stderr, "ftnpkt dump: unknown --chrs \"%s\", showing text as-is\n", fchrs);

    packet_t pkt;
    if (packet_read_file(path, &pkt, err, sizeof err) != 0) {
        fprintf(stderr, "ftnpkt dump: %s\n", err);
        parsed_free(&p);
        return 1;
    }

    int use_color = (strcmp(cmode, "always") == 0) ||
                    (strcmp(cmode, "auto") == 0 && isatty(1));

    sbuf out; sbuf_init(&out);
    dump_packet(&pkt, use_color, notation, fchrs, &out);
    fwrite(out.data, 1, out.len, stdout);
    sbuf_free(&out);
    packet_free(&pkt);
    parsed_free(&p);
    return 0;
}
