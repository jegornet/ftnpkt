#include "commands.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "addr.h"
#include "args.h"
#include "packet.h"
#include "util.h"

static const flag_def_t DEFS[] = {
    { "type",         "packet variant (auto: 2+ if --from-addr is a point, else 2e)" },
    { "baud",         "baud field" },
    { "password",     "packet password (<=8 chars, ASCII)" },
    { "prod-code-lo", "low FTSC product code (0xFE by convention)" },
    { "from-addr",    "packet source address Z:N/N[.P] (required)" },
    { "to-addr",      "packet destination address Z:N/N[.P] (required)" },
};

static const flag_set_t FS = {
    .name = "ftnpkt create",
    .usage_intro =
        "Usage: ftnpkt create <file.pkt> [options]\n"
        "  Create an empty packet. Options define the packet header ONLY;\n"
        "  use \"ftnpkt addmsg\" to add messages.",
    .defs = DEFS, .ndefs = sizeof DEFS / sizeof DEFS[0],
};

static void make_password(const char *raw, uint8_t pw[FTN_PASSWORD_SIZE])
{
    memset(pw, 0, FTN_PASSWORD_SIZE);
    size_t n = raw ? strlen(raw) : 0;
    if (n > FTN_PASSWORD_SIZE) n = FTN_PASSWORD_SIZE;
    memcpy(pw, raw, n);
}

int cmd_create(int argc, char **argv)
{
    parsed_t p;
    char err[256];
    sbuf usage;
    sbuf_init(&usage);

    if (flag_set_parse(&FS, argc, argv, &p, err, sizeof err) != 0) {
        fprintf(stderr, "ftnpkt create: %s\n", err);
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

    const char *typ = parsed_get(&p, &FS, "type");
    if (typ && strcmp(typ, "2") != 0 && strcmp(typ, "2e") != 0 && strcmp(typ, "2+") != 0) {
        fprintf(stderr, "ftnpkt create: invalid --type \"%s\" (expected 2, 2e or 2+)\n", typ);
        parsed_free(&p);
        return 2;
    }

    const char *from_str = parsed_get(&p, &FS, "from-addr");
    const char *to_str   = parsed_get(&p, &FS, "to-addr");
    if (!from_str || !to_str) {
        fprintf(stderr, "ftnpkt create: --from-addr and --to-addr are required\n");
        parsed_free(&p);
        return 2;
    }
    ftn_addr_t from, to;
    if (ftn_parse_addr(from_str, &from, err, sizeof err) != 0 ||
        ftn_parse_addr(to_str,   &to,   err, sizeof err) != 0) {
        fprintf(stderr, "ftnpkt create: %s\n", err);
        parsed_free(&p);
        return 2;
    }

    uint16_t baud = 0;
    const char *baud_s = parsed_get(&p, &FS, "baud");
    if (baud_s) { uint16_t v; if (parse_u16_base0(baud_s, &v) == 0) baud = v; }

    uint16_t prod_lo = 0xFE;
    const char *prod_s = parsed_get(&p, &FS, "prod-code-lo");
    if (prod_s) { uint16_t v; if (parse_u16_base0(prod_s, &v) == 0) prod_lo = v; }

    const char *chosen = typ;
    if (!chosen) {
        chosen = from.point != 0 ? "2+" : "2e";
    }

    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);

    packet_t pkt;
    packet_init(&pkt);
    pkt.orig_node     = from.node;
    pkt.dest_node     = to.node;
    pkt.year          = (uint16_t)(tm.tm_year + 1900);
    pkt.month         = (uint16_t)tm.tm_mon;
    pkt.day           = (uint16_t)tm.tm_mday;
    pkt.hour          = (uint16_t)tm.tm_hour;
    pkt.minute        = (uint16_t)tm.tm_min;
    pkt.second        = (uint16_t)tm.tm_sec;
    pkt.baud          = baud;
    pkt.orig_net      = from.net;
    pkt.dest_net      = to.net;
    pkt.prod_code_lo  = (uint8_t)prod_lo;
    pkt.prod_rev_major = 1;
    make_password(parsed_get(&p, &FS, "password"), pkt.password);
    pkt.orig_zone     = from.zone;
    pkt.dest_zone     = to.zone;
    pkt.cap_word      = FTN_CAP_WORD;
    pkt.cap_valid     = (uint16_t)((FTN_CAP_WORD << 8) | (FTN_CAP_WORD >> 8));

    if (strcmp(chosen, "2") == 0) {
        pkt.type = PKT_TYPE2;
        if (from.point != 0 || to.point != 0)
            fprintf(stderr, "ftnpkt create: warning: Type-2 (Stone Age) cannot carry point addresses; point fields ignored.\n");
    } else if (strcmp(chosen, "2e") == 0) {
        pkt.type = PKT_TYPE2E;
        pkt.q_orig_zone = from.zone; pkt.q_dest_zone = to.zone;
        pkt.aux_net = 0; pkt.prod_code_hi = 0; pkt.prod_rev_minor = 1;
        pkt.orig_point = from.point; pkt.dest_point = to.point;
    } else { /* 2+ */
        pkt.type = PKT_TYPE2PLUS;
        pkt.q_orig_zone = from.zone; pkt.q_dest_zone = to.zone;
        pkt.aux_net = 0; pkt.prod_code_hi = 0; pkt.prod_rev_minor = 1;
        pkt.orig_point = from.point; pkt.dest_point = to.point;
    }

    char e4[FTN_ADDR_BUFSZ], d4[FTN_ADDR_BUFSZ];
    ftn_addr4d(from, e4);
    ftn_addr4d(to, d4);

    char werr[256];
    if (packet_write_file(path, &pkt, werr, sizeof werr) != 0) {
        fprintf(stderr, "ftnpkt create: %s\n", werr);
        packet_free(&pkt);
        parsed_free(&p);
        return 1;
    }
    printf("Created %s (empty %s packet) %s -> %s\n", path, chosen, e4, d4);
    packet_free(&pkt);
    parsed_free(&p);
    return 0;
}
