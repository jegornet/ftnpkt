#include "message.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "charset.h"
#include "util.h"

void message_args_init(message_args_t *a)
{
    memset(a, 0, sizeof *a);
    a->to_name = strdup("All");
    a->attr    = MSG_DEFAULT_ATTR;
    a->charset = strdup("utf-8");
}

static void free_str(char **s) { free(*s); *s = NULL; }

void message_args_free(message_args_t *a)
{
    free_str(&a->from_name);
    free_str(&a->to_name);
    free_str(&a->subject);
    free_str(&a->charset);
    free_str(&a->areatag);
    free_str(&a->msg_id);
    free_str(&a->tear_line);
    free_str(&a->origin);
    free_str(&a->seen_by);
    free_str(&a->path);
    for (size_t i = 0; i < a->add_kludge_count; i++) free(a->add_kludge[i]);
    free(a->add_kludge);
    a->add_kludge = NULL;
    a->add_kludge_count = 0;
}

int resolve_addresses(const message_args_t *a, ftn_addr_t *orig, ftn_addr_t *dest,
                      char *errmsg, size_t errsz)
{
    *orig = a->from_addr;
    if (a->has_to_addr) { *dest = a->to_addr; return 0; }
    if (a->areatag && a->areatag[0]) {
        *dest = a->from_addr;
        dest->point = 0;
        return 0;
    }
    snprintf(errmsg, errsz, "%s", MSG_ERR_MISSING_DEST);
    return -1;
}

/* "--- <tearline>" with trailing spaces trimmed (collapses to "---" if empty). */
static void append_tear_line(const char *tearline, sbuf *out)
{
    sbuf_append_cstr(out, "--- ");
    sbuf_append_cstr(out, tearline ? tearline : "");
    while (out->len > 0 && out->data[out->len - 1] == ' ') {
        out->len--;
        out->data[out->len] = '\0';
    }
}

/* Build the UTF-8 message body (CR-terminated lines) into `body`. */
static void build_body(const char *text, const char *areatag, const char *msgid,
                       const char *tearline, const char *origin,
                       const char *seen_by, const char *path,
                       ftn_addr_t orig, ftn_addr_t dest,
                       char *const *kludges, size_t nkludges, sbuf *body)
{
    char buf[FTN_ADDR_BUFSZ];
    if (areatag && areatag[0]) {
        sbuf_append_cstr(body, "AREA:");
        sbuf_append_cstr(body, areatag);
        sbuf_push(body, '\r');
    }
    sbuf_append_cstr(body, "\x01MSGID: ");
    sbuf_append_cstr(body, msgid);
    sbuf_push(body, '\r');

    if (!areatag || !areatag[0]) {
        ftn_addr3d(dest, buf);                 /* INTL dest orig */
        sbuf_append_cstr(body, "\x01INTL ");
        sbuf_append_cstr(body, buf);
        sbuf_push(body, ' ');
        ftn_addr3d(orig, buf);
        sbuf_append_cstr(body, buf);
        sbuf_push(body, '\r');
        if (dest.point != 0) sbuf_append_printf(body, "\x01TOPT %u\r", dest.point);
        if (orig.point != 0) sbuf_append_printf(body, "\x01FMPT %u\r", orig.point);
    }
    for (size_t i = 0; i < nkludges; i++) {
        sbuf_push(body, '\x01');
        sbuf_append_cstr(body, kludges[i]);
        sbuf_push(body, '\r');
    }

    char *norm = normalize_cr(text ? text : "");
    sbuf_append_cstr(body, norm);
    free(norm);
    sbuf_push(body, '\r');

    append_tear_line(tearline, body);
    sbuf_push(body, '\r');

    ftn_addr4d(orig, buf);
    sbuf_append_printf(body, " * Origin: %s (%s)\r", origin, buf);

    if (areatag && areatag[0]) {
        sbuf_append_cstr(body, "SEEN-BY: ");
        sbuf_append_cstr(body, seen_by);
        sbuf_push(body, '\r');
        sbuf_append_cstr(body, "\x01PATH: ");
        sbuf_append_cstr(body, path);
        sbuf_push(body, '\r');
    }
}

/* Encode a UTF-8 field into charset bytes and store into a packed_msg field. */
static int enc_field(const char *charset, const char *utf8,
                     uint8_t **dst, size_t *len, char *errmsg, size_t errsz)
{
    sbuf tmp;
    sbuf_init(&tmp);
    if (charset_encode(charset, utf8, &tmp) != 0) {
        sbuf_free(&tmp);
        snprintf(errmsg, errsz, "unsupported charset \"%s\"", charset);
        return -1;
    }
    *dst = (uint8_t *)bytes_dup(tmp.data, tmp.len);
    *len = tmp.len;
    sbuf_free(&tmp);
    if (!*dst) { snprintf(errmsg, errsz, "out of memory"); return -1; }
    return 0;
}

int build_message_from_args(const message_args_t *a, const char *text,
                            ftn_addr_t orig, ftn_addr_t dest,
                            packed_msg_t *out, char *errmsg, size_t errsz)
{
    packed_msg_init(out);

    /* Resolve defaults. */
    char *msgid   = a->msg_id && a->msg_id[0] ? a->msg_id : NULL;
    const char *origin = (a->origin && a->origin[0]) ? a->origin : MSG_DEFAULT_ORIGIN;
    sbuf seenby, pathbuf, msgidbuf, dt;
    sbuf_init(&seenby); sbuf_init(&pathbuf); sbuf_init(&msgidbuf); sbuf_init(&dt);

    if (!msgid) {
        char a4[FTN_ADDR_BUFSZ];
        ftn_addr4d(orig, a4);
        sbuf_append_cstr(&msgidbuf, a4);
        sbuf_push(&msgidbuf, ' ');
        random_hex(4, &msgidbuf);
        msgid = msgidbuf.data;
    }
    if (!a->seen_by || !a->seen_by[0]) {
        char a2[FTN_ADDR_BUFSZ];
        ftn_addr2d(orig, a2);
        sbuf_append_cstr(&seenby, a2);
    } else {
        sbuf_append_cstr(&seenby, a->seen_by);
    }
    if (!a->path || !a->path[0]) {
        char a2[FTN_ADDR_BUFSZ];
        ftn_addr2d(orig, a2);
        sbuf_append_cstr(&pathbuf, a2);
    } else {
        sbuf_append_cstr(&pathbuf, a->path);
    }

    /* Body + dateTime in UTF-8, then encoded. */
    sbuf body;
    sbuf_init(&body);
    build_body(text, a->areatag, msgid, a->tear_line, origin,
               seenby.data, pathbuf.data, orig, dest,
               a->add_kludge, a->add_kludge_count, &body);
    fido_date_time_now(&dt);

    int rc = 0;
    if (enc_field(a->charset, body.data, &out->text, &out->text_len, errmsg, errsz) != 0) rc = -1;
    if (rc == 0 && enc_field(a->charset, dt.data, &out->date_time, &out->date_time_len, errmsg, errsz) != 0) rc = -1;
    if (rc == 0 && enc_field(a->charset, a->to_name, &out->to_user_name, &out->to_user_name_len, errmsg, errsz) != 0) rc = -1;
    if (rc == 0 && enc_field(a->charset, a->from_name, &out->from_user_name, &out->from_user_name_len, errmsg, errsz) != 0) rc = -1;
    if (rc == 0 && enc_field(a->charset, a->subject, &out->subject, &out->subject_len, errmsg, errsz) != 0) rc = -1;

    if (rc == 0) {
        out->orig_node = orig.node; out->dest_node = dest.node;
        out->orig_net  = orig.net;  out->dest_net  = dest.net;
        out->attribute = a->attr;   out->cost      = 0;
    }

    sbuf_free(&seenby); sbuf_free(&pathbuf); sbuf_free(&msgidbuf);
    sbuf_free(&dt); sbuf_free(&body);
    if (rc != 0) packed_msg_free(out);
    return rc;
}
