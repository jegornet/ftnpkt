#include "commands.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "addr.h"
#include "args.h"
#include "message.h"
#include "packet.h"
#include "util.h"

static const flag_def_t DEFS[] = {
    { "from",      "sender name (required)" },
    { "from-addr", "sender address Z:N/N[.P] (required)" },
    { "to",        "recipient name" },
    { "to-addr",   "recipient address Z:N/N[.P]; required when --areatag is not given (netmail), otherwise defaults to the sender address with point dropped" },
    { "subject",   "subject line (required)" },
    { "attr",      "attribute word (bit flags, e.g. 0x100 Local, 0x002 Crash)" },
    { "charset",   "charset for text fields" },
    { "areatag",   "echomail area tag (omit for netmail: no AREA line)" },
    { "msgid",     "MSGID value (derived: '<orig 4D> <random hex>')" },
    { "tearline",  "tearline after tear line" },
    { "origin",    "origin text (derived: 'ftnpkt')" },
    { "seen-by",   "SEEN-BY value (derived: '<orig net>/<orig node>')" },
    { "path",      "PATH value (derived: '<orig net>/<orig node>')" },
    { "addkludge", "extra kludge line added after MSGID/INTL/TOPT/FMPT (without the leading ^A); may be repeated, e.g. --addkludge \"CHRS: UTF-8 4\"" },
};

static const flag_set_t FS = {
    .name = "ftnpkt addmsg",
    .usage_intro =
        "Usage: ftnpkt addmsg <file.pkt> <text> [options]\n"
        "  Append a message to an existing packet. <text> is the message body\n"
        "  (use '-' to read from stdin). Options define the message ONLY;\n"
        "  the packet header is left unchanged.",
    .defs = DEFS, .ndefs = sizeof DEFS / sizeof DEFS[0],
};

static char *xstrdup(const char *s) { char *r = strdup(s ? s : ""); return r; }

static int build_message_args(const parsed_t *p, message_args_t *a, char *err, size_t errsz)
{
    message_args_init(a);
    free(a->from_name); a->from_name = xstrdup(parsed_get(p, &FS, "from"));

    const char *fa = parsed_get(p, &FS, "from-addr");
    if (ftn_parse_addr(fa ? fa : "", &a->from_addr, err, errsz) != 0) return -1;

    free(a->to_name);
    const char *to = parsed_get(p, &FS, "to");
    a->to_name = xstrdup(to ? to : "All");

    if (parsed_seen(p, &FS, "to-addr")) {
        const char *ta = parsed_get(p, &FS, "to-addr");
        if (ftn_parse_addr(ta ? ta : "", &a->to_addr, err, errsz) != 0) return -1;
        a->has_to_addr = 1;
    }

    free(a->subject);  a->subject  = xstrdup(parsed_get(p, &FS, "subject"));
    const char *at = parsed_get(p, &FS, "attr");
    if (at) { uint16_t v; if (parse_u16_base0(at, &v) == 0) a->attr = v; }
    free(a->charset);  a->charset  = xstrdup(parsed_get(p, &FS, "charset") ? parsed_get(p, &FS, "charset") : "utf-8");
    free(a->areatag);  a->areatag  = xstrdup(parsed_get(p, &FS, "areatag"));
    free(a->msg_id);   a->msg_id   = xstrdup(parsed_get(p, &FS, "msgid"));
    free(a->tear_line); a->tear_line = xstrdup(parsed_get(p, &FS, "tearline"));
    free(a->origin);   a->origin   = xstrdup(parsed_get(p, &FS, "origin"));
    free(a->seen_by);  a->seen_by  = xstrdup(parsed_get(p, &FS, "seen-by"));
    free(a->path);     a->path     = xstrdup(parsed_get(p, &FS, "path"));

    size_t nk = 0;
    const char **kl = parsed_get_all(p, &FS, "addkludge", &nk);
    if (nk) {
        a->add_kludge = calloc(nk, sizeof *a->add_kludge);
        if (!a->add_kludge) { snprintf(err, errsz, "out of memory"); return -1; }
        for (size_t i = 0; i < nk; i++) {
            a->add_kludge[i] = xstrdup(kl[i]);
            if (!a->add_kludge[i]) { snprintf(err, errsz, "out of memory"); return -1; }
            a->add_kludge_count++;
        }
    }
    return 0;
}

static int validate(const parsed_t *p, const message_args_t *a, char *err, size_t errsz)
{
    const char *missing[4];
    size_t n = 0;
    if (!parsed_seen(p, &FS, "from") || !a->from_name[0]) missing[n++] = "--from";
    if (!parsed_seen(p, &FS, "from-addr")) missing[n++] = "--from-addr";
    if (!parsed_seen(p, &FS, "subject") || !a->subject[0]) missing[n++] = "--subject";
    if (n == 0) return 0;
    sbuf sb; sbuf_init(&sb);
    sbuf_append_cstr(&sb, "the following arguments are required: ");
    for (size_t i = 0; i < n; i++) {
        if (i) sbuf_append_cstr(&sb, ", ");
        sbuf_append_cstr(&sb, missing[i]);
    }
    snprintf(err, errsz, "%s", sb.data ? sb.data : "");
    sbuf_free(&sb);
    return -1;
}

static char *read_stdin(void)
{
    sbuf sb; sbuf_init(&sb);
    char tmp[4096];
    size_t n;
    while ((n = fread(tmp, 1, sizeof tmp, stdin)) > 0) {
        if (sbuf_append(&sb, tmp, n) != 0) { sbuf_free(&sb); return NULL; }
    }
    if (!sb.data) { sbuf_push(&sb, '\0'); sb.len = 0; }
    return sb.data;
}

int cmd_addmsg(int argc, char **argv)
{
    parsed_t p;
    char err[256];
    sbuf usage; sbuf_init(&usage);

    if (flag_set_parse(&FS, argc, argv, &p, err, sizeof err) != 0) {
        fprintf(stderr, "ftnpkt addmsg: %s\n", err);
        flag_set_usage(&FS, &usage);
        fprintf(stderr, "%s", usage.data ? usage.data : "");
        sbuf_free(&usage);
        return 2;
    }
    if (p.npos != 2) {
        flag_set_usage(&FS, &usage);
        fprintf(stderr, "%s", usage.data ? usage.data : "");
        sbuf_free(&usage);
        parsed_free(&p);
        return 2;
    }
    sbuf_free(&usage);

    const char *path = p.positionals[0];
    char *text = strdup(p.positionals[1]);
    if (!text) { fprintf(stderr, "ftnpkt addmsg: out of memory\n"); parsed_free(&p); return 1; }

    message_args_t m;
    if (build_message_args(&p, &m, err, sizeof err) != 0) {
        fprintf(stderr, "ftnpkt addmsg: %s\n", err);
        message_args_free(&m); free(text); parsed_free(&p);
        return 2;
    }
    if (validate(&p, &m, err, sizeof err) != 0) {
        fprintf(stderr, "ftnpkt addmsg: %s\n", err);
        message_args_free(&m); free(text); parsed_free(&p);
        return 2;
    }

    if (strcmp(text, "-") == 0) {
        free(text);
        text = read_stdin();
        if (!text) { fprintf(stderr, "ftnpkt addmsg: failed to read stdin\n");
            message_args_free(&m); parsed_free(&p); return 1; }
    }

    ftn_addr_t orig, dest;
    if (resolve_addresses(&m, &orig, &dest, err, sizeof err) != 0) {
        fprintf(stderr, "ftnpkt addmsg: %s\n", err);
        free(text); message_args_free(&m); parsed_free(&p);
        return 2;
    }

    packet_t pkt;
    if (packet_read_file(path, &pkt, err, sizeof err) != 0) {
        fprintf(stderr, "ftnpkt addmsg: %s\n", err);
        free(text); message_args_free(&m); parsed_free(&p);
        return 1;
    }

    packed_msg_t msg;
    if (build_message_from_args(&m, text, orig, dest, &msg, err, sizeof err) != 0) {
        fprintf(stderr, "ftnpkt addmsg: %s\n", err);
        packet_free(&pkt); free(text); message_args_free(&m); parsed_free(&p);
        return 1;
    }
    free(text);

    if (packet_add_msg(&pkt, &msg) != 0) {
        fprintf(stderr, "ftnpkt addmsg: out of memory\n");
        packed_msg_free(&msg); packet_free(&pkt); message_args_free(&m); parsed_free(&p);
        return 1;
    }
    packed_msg_free(&msg);

    if (packet_write_file(path, &pkt, err, sizeof err) != 0) {
        fprintf(stderr, "ftnpkt addmsg: %s\n", err);
        packet_free(&pkt); message_args_free(&m); parsed_free(&p);
        return 1;
    }

    char o4[FTN_ADDR_BUFSZ], d4[FTN_ADDR_BUFSZ];
    ftn_addr4d(orig, o4);
    ftn_addr4d(dest, d4);
    printf("Added message to %s: now %zu message(s) (from %s @ %s to %s @ %s)\n",
           path, pkt.msg_count, m.from_name, o4, m.to_name, d4);

    packet_free(&pkt); message_args_free(&m); parsed_free(&p);
    return 0;
}
