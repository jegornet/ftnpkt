#include "packet.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- little-endian helpers ---- */
static uint16_t rd_u16(const uint8_t *d, size_t off)
{
    return (uint16_t)((uint16_t)d[off] | ((uint16_t)d[off + 1] << 8));
}

static void put_u16(sbuf *out, uint16_t v)
{
    uint8_t b[2] = { (uint8_t)(v & 0xff), (uint8_t)(v >> 8) };
    sbuf_append(out, b, 2);
}

static uint16_t swap16(uint16_t v) { return (uint16_t)((v << 8) | (v >> 8)); }

/* ---- packed message ---- */
void packed_msg_init(packed_msg_t *m)
{
    memset(m, 0, sizeof *m);
}

void packed_msg_free(packed_msg_t *m)
{
    if (!m) return;
    free(m->date_time);     m->date_time = NULL;
    free(m->to_user_name);  m->to_user_name = NULL;
    free(m->from_user_name); m->from_user_name = NULL;
    free(m->subject);       m->subject = NULL;
    free(m->text);          m->text = NULL;
    m->date_time_len = m->to_user_name_len = m->from_user_name_len
        = m->subject_len = m->text_len = 0;
}

int pm_has(const packed_msg_t *m, uint16_t flag) { return (m->attribute & flag) != 0; }

int packed_msg_serialize(const packed_msg_t *m, sbuf *out)
{
    /* Fixed 14-byte header (7 x u16 LE). */
    put_u16(out, 0x0002);                  /* message type */
    put_u16(out, m->orig_node);
    put_u16(out, m->dest_node);
    put_u16(out, m->orig_net);
    put_u16(out, m->dest_net);
    put_u16(out, m->attribute);
    put_u16(out, m->cost);

    /* 20-byte dateTime: content truncated to 19, NUL-padded. */
    uint8_t dt[20];
    memset(dt, 0, sizeof dt);
    size_t dn = m->date_time_len < 19 ? m->date_time_len : 19;
    if (dn) memcpy(dt, m->date_time, dn);
    sbuf_append(out, dt, 20);

    sbuf_append(out, m->to_user_name, m->to_user_name_len);   sbuf_push(out, '\0');
    sbuf_append(out, m->from_user_name, m->from_user_name_len); sbuf_push(out, '\0');
    sbuf_append(out, m->subject, m->subject_len);              sbuf_push(out, '\0');
    sbuf_append(out, m->text, m->text_len);                    sbuf_push(out, '\0');
    return 0;
}

/* ---- packet ---- */
void packet_init(packet_t *p) { memset(p, 0, sizeof *p); p->type = PKT_TYPE2; }

void packet_free(packet_t *p)
{
    if (!p) return;
    for (size_t i = 0; i < p->msg_count; i++) packed_msg_free(&p->messages[i]);
    free(p->messages);
    p->messages = NULL;
    p->msg_count = p->msg_cap = 0;
}

int packet_add_msg(packet_t *p, const packed_msg_t *m)
{
    if (p->msg_count == p->msg_cap) {
        size_t cap = p->msg_cap ? p->msg_cap * 2 : 4;
        packed_msg_t *arr = realloc(p->messages, cap * sizeof *arr);
        if (!arr) return -1;
        p->messages = arr;
        p->msg_cap = cap;
    }
    packed_msg_t *dst = &p->messages[p->msg_count++];
    packed_msg_init(dst);
    dst->orig_node = m->orig_node; dst->dest_node = m->dest_node;
    dst->orig_net = m->orig_net;   dst->dest_net = m->dest_net;
    dst->attribute = m->attribute; dst->cost = m->cost;
    if (m->date_time_len)   { dst->date_time = bytes_dup(m->date_time, m->date_time_len); }
    if (m->to_user_name_len){ dst->to_user_name = bytes_dup(m->to_user_name, m->to_user_name_len); }
    if (m->from_user_name_len){ dst->from_user_name = bytes_dup(m->from_user_name, m->from_user_name_len); }
    if (m->subject_len)     { dst->subject = bytes_dup(m->subject, m->subject_len); }
    if (m->text_len)        { dst->text = bytes_dup(m->text, m->text_len); }
    dst->date_time_len = m->date_time_len;
    dst->to_user_name_len = m->to_user_name_len;
    dst->from_user_name_len = m->from_user_name_len;
    dst->subject_len = m->subject_len;
    dst->text_len = m->text_len;
    return 0;
}

static void write_prefix(const packet_t *p, sbuf *out)
{
    put_u16(out, p->orig_node);
    put_u16(out, p->dest_node);
    put_u16(out, p->year);
    put_u16(out, p->month);
    put_u16(out, p->day);
    put_u16(out, p->hour);
    put_u16(out, p->minute);
    put_u16(out, p->second);
    put_u16(out, p->baud);
    put_u16(out, FTN_PKT_VER);
}

int packet_serialize(const packet_t *p, sbuf *out)
{
    write_prefix(p, out);

    uint16_t wire_orig_net = p->orig_net;
    uint16_t wire_aux      = p->aux_net;
    if (p->type == PKT_TYPE2PLUS && p->orig_point != 0) {
        wire_orig_net = 0xFFFF;            /* poison; real net in auxNet */
        wire_aux      = p->orig_net;
    }
    put_u16(out, wire_orig_net);
    put_u16(out, p->dest_net);
    sbuf_push(out, (char)p->prod_code_lo);
    sbuf_push(out, (char)p->prod_rev_major);
    sbuf_append(out, p->password, FTN_PASSWORD_SIZE);

    if (p->type == PKT_TYPE2) {
        put_u16(out, p->orig_zone);
        put_u16(out, p->dest_zone);
        sbuf_append(out, p->fill, FTN_FILL_SIZE);
    } else {
        put_u16(out, p->q_orig_zone);
        put_u16(out, p->q_dest_zone);
        put_u16(out, wire_aux);
        put_u16(out, p->cap_valid);
        sbuf_push(out, (char)p->prod_code_hi);
        sbuf_push(out, (char)p->prod_rev_minor);
        put_u16(out, p->cap_word);
        put_u16(out, p->orig_zone);
        put_u16(out, p->dest_zone);
        put_u16(out, p->orig_point);
        put_u16(out, p->dest_point);
        sbuf_append(out, p->prod_data, FTN_PROD_DATA_SIZE);
    }

    for (size_t i = 0; i < p->msg_count; i++)
        packed_msg_serialize(&p->messages[i], out);
    put_u16(out, 0x0000);                  /* terminator */
    return 0;
}

/* ---- parsing ---- */
static int set_field(uint8_t **dst, size_t *len, const uint8_t *src, size_t n)
{
    uint8_t *copy = bytes_dup(src, n);
    if (!copy) return -1;
    *dst = copy;
    *len = n;
    return 0;
}

/* Read a NUL-terminated field at offset; stores content + next offset. */
static int read_cstr(const uint8_t *data, size_t len, size_t offset,
                     uint8_t **dst, size_t *outlen, size_t *next)
{
    size_t i = offset;
    while (i < len && data[i] != 0) i++;
    size_t content = i - offset;
    if (set_field(dst, outlen, data + offset, content) != 0) return -1;
    *next = (i < len) ? i + 1 : len;        /* skip NUL if present */
    return 0;
}

static int parse_one_msg(const uint8_t *data, size_t len, size_t offset,
                         packed_msg_t *m, size_t *next, char *err, size_t errsz)
{
    if (offset + 14 > len) {
        snprintf(err, errsz, "packed message: truncated header at offset %zu", offset);
        return -1;
    }
    packed_msg_init(m);
    if (rd_u16(data, offset) != 0x0002) {
        snprintf(err, errsz, "invalid packed message type: 0x%04x, expected 0x0002",
                 rd_u16(data, offset));
        return -1;
    }
    m->orig_node = rd_u16(data, offset + 2);
    m->dest_node = rd_u16(data, offset + 4);
    m->orig_net  = rd_u16(data, offset + 6);
    m->dest_net  = rd_u16(data, offset + 8);
    m->attribute = rd_u16(data, offset + 10);
    m->cost      = rd_u16(data, offset + 12);

    size_t pos = offset + 14;
    if (pos + 20 > len) {
        snprintf(err, errsz, "packed message: truncated dateTime at offset %zu", pos);
        return -1;
    }
    /* dateTime: 20 bytes, content up to first NUL. */
    size_t dt_end = pos;
    while (dt_end < pos + 20 && data[dt_end] != 0) dt_end++;
    if (set_field(&m->date_time, &m->date_time_len, data + pos, dt_end - pos) != 0) goto oom;
    pos += 20;

    if (read_cstr(data, len, pos, &m->to_user_name, &m->to_user_name_len, &pos) != 0) goto oom;
    if (read_cstr(data, len, pos, &m->from_user_name, &m->from_user_name_len, &pos) != 0) goto oom;
    if (read_cstr(data, len, pos, &m->subject, &m->subject_len, &pos) != 0) goto oom;
    if (read_cstr(data, len, pos, &m->text, &m->text_len, &pos) != 0) goto oom;
    *next = pos;
    return 0;
oom:
    snprintf(err, errsz, "out of memory while parsing message");
    return -1;
}

static int parse_messages(const uint8_t *data, size_t len, size_t offset,
                          packet_t *p, char *err, size_t errsz)
{
    size_t pos = offset;
    while (pos + 1 < len) {
        if (rd_u16(data, pos) == 0) break;     /* terminator */
        packed_msg_t m;
        size_t next;
        if (parse_one_msg(data, len, pos, &m, &next, err, errsz) != 0) return -1;
        if (packet_add_msg(p, &m) != 0) {
            packed_msg_free(&m);
            snprintf(err, errsz, "out of memory while storing message");
            return -1;
        }
        packed_msg_free(&m);                   /* add_msg deep-copied */
        pos = next;
    }
    return 0;
}

static void parse_common(const uint8_t *d, packet_t *p)
{
    p->orig_node = rd_u16(d, 0x00); p->dest_node = rd_u16(d, 0x02);
    p->year   = rd_u16(d, 0x04);    p->month  = rd_u16(d, 0x06);
    p->day    = rd_u16(d, 0x08);    p->hour   = rd_u16(d, 0x0A);
    p->minute = rd_u16(d, 0x0C);    p->second = rd_u16(d, 0x0E);
    p->baud   = rd_u16(d, 0x10);
    memcpy(p->password, d + 0x1A, FTN_PASSWORD_SIZE);
    p->prod_code_lo   = d[0x18];   p->prod_rev_major = d[0x19];
}

static int parse_bytes(const uint8_t *d, size_t len, packet_t *p, char *err, size_t errsz)
{
    packet_init(p);
    if (len < FTN_HEADER_SIZE) {
        snprintf(err, errsz, "packet too small: %zu bytes, minimum %u", len, FTN_HEADER_SIZE);
        return -1;
    }
    if (rd_u16(d, 0x12) != FTN_PKT_VER) {
        snprintf(err, errsz, "invalid packet version: %u, expected 2", rd_u16(d, 0x12));
        return -1;
    }
    parse_common(d, p);

    uint16_t cap_word = rd_u16(d, 0x2C);
    uint16_t cap_valid = rd_u16(d, 0x28);

    if (cap_word == 0) {
        /* Type-2 Stone Age */
        p->type = PKT_TYPE2;
        p->orig_net = rd_u16(d, 0x14); p->dest_net = rd_u16(d, 0x16);
        p->orig_zone = rd_u16(d, 0x22); p->dest_zone = rd_u16(d, 0x24);
        memcpy(p->fill, d + 0x26, FTN_FILL_SIZE);
        return parse_messages(d, len, FTN_HEADER_SIZE, p, err, errsz);
    }

    if (swap16(cap_word) != cap_valid) {
        snprintf(err, errsz,
                 "CapabilityWord validation error: capWord=0x%04x, capValid=0x%04x",
                 cap_word, cap_valid);
        return -1;
    }

    uint16_t raw_orig_net = rd_u16(d, 0x14);
    uint16_t orig_point   = rd_u16(d, 0x32);
    p->dest_net = rd_u16(d, 0x16);
    p->q_orig_zone = rd_u16(d, 0x22); p->q_dest_zone = rd_u16(d, 0x24);
    p->aux_net     = rd_u16(d, 0x26);
    p->cap_valid   = cap_valid;
    p->prod_code_hi = d[0x2A]; p->prod_rev_minor = d[0x2B];
    p->cap_word    = cap_word;
    p->orig_zone   = rd_u16(d, 0x2E); p->dest_zone = rd_u16(d, 0x30);
    p->orig_point  = orig_point;      p->dest_point = rd_u16(d, 0x34);
    memcpy(p->prod_data, d + 0x36, FTN_PROD_DATA_SIZE);

    int is_plus = (raw_orig_net == 0xFFFF && orig_point != 0);
    p->type = is_plus ? PKT_TYPE2PLUS : PKT_TYPE2E;
    /* recover real OrigNet from AuxNet for point-originated Type-2+ */
    p->orig_net = is_plus ? p->aux_net : raw_orig_net;

    return parse_messages(d, len, FTN_HEADER_SIZE, p, err, errsz);
}

int packet_read_file(const char *path, packet_t *out, char *errmsg, size_t errsz)
{
    FILE *f = fopen(path, "rb");
    if (!f) { snprintf(errmsg, errsz, "cannot open %s: %s", path, strerror(errno)); return -1; }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); snprintf(errmsg, errsz, "seek failed"); return -1; }
    long sz = ftell(f);
    if (sz < 0) { fclose(f); snprintf(errmsg, errsz, "tell failed"); return -1; }
    rewind(f);
    uint8_t *data = malloc((size_t)sz ? (size_t)sz : 1);
    if (!data) { fclose(f); snprintf(errmsg, errsz, "out of memory"); return -1; }
    size_t got = fread(data, 1, (size_t)sz, f);
    fclose(f);
    if (got != (size_t)sz) {
        free(data);
        snprintf(errmsg, errsz, "short read on %s", path);
        return -1;
    }
    int rc = parse_bytes(data, got, out, errmsg, errsz);
    free(data);
    return rc;
}

int packet_write_file(const char *path, packet_t *p, char *errmsg, size_t errsz)
{
    sbuf out;
    sbuf_init(&out);
    packet_serialize(p, &out);
    FILE *f = fopen(path, "wb");
    if (!f) {
        snprintf(errmsg, errsz, "cannot open %s for writing", path);
        sbuf_free(&out);
        return -1;
    }
    size_t wrote = fwrite(out.data, 1, out.len, f);
    int ok = (wrote == out.len);
    if (!ok) snprintf(errmsg, errsz, "short write on %s", path);
    if (fclose(f) != 0) ok = 0;
    sbuf_free(&out);
    return ok ? 0 : -1;
}
