#ifndef FTNPKT_PACKET_H
#define FTNPKT_PACKET_H

#include <stddef.h>
#include <stdint.h>

#include "util.h"

#define FTN_HEADER_SIZE   58u
#define FTN_PASSWORD_SIZE 8u
#define FTN_PROD_DATA_SIZE 4u
#define FTN_FILL_SIZE     20u
#define FTN_PKT_VER       2u
#define FTN_CAP_WORD      0x0001u   /* Type-2+ capability word (FSC-0039/0048) */

typedef enum { PKT_TYPE2 = 0, PKT_TYPE2E = 1, PKT_TYPE2PLUS = 2 } pkt_type_t;

/* ---- packed message attribute-word bits (FTS-0001 C.1) ---- */
#define ATTR_PRIVATE               0x0001u
#define ATTR_CRASH                 0x0002u
#define ATTR_RECD                  0x0004u
#define ATTR_SENT                  0x0008u
#define ATTR_FILE_ATTACHED         0x0010u
#define ATTR_IN_TRANSIT            0x0020u
#define ATTR_ORPHAN                0x0040u
#define ATTR_KILL_SENT             0x0080u
#define ATTR_LOCAL                 0x0100u
#define ATTR_HOLD_FOR_PICKUP       0x0200u
#define ATTR_FILE_REQUEST          0x0800u
#define ATTR_RETURN_RECEIPT_REQ    0x1000u
#define ATTR_RETURN_RECEIPT        0x2000u
#define ATTR_AUDIT_REQUEST         0x4000u
#define ATTR_FILE_UPDATE_REQ       0x8000u

/* A FTS-0001 packed message. Variable fields are raw bytes with explicit length. */
typedef struct {
    uint16_t orig_node, dest_node, orig_net, dest_net, attribute, cost;
    uint8_t *date_time;     size_t date_time_len;
    uint8_t *to_user_name;  size_t to_user_name_len;
    uint8_t *from_user_name; size_t from_user_name_len;
    uint8_t *subject;       size_t subject_len;
    uint8_t *text;          size_t text_len;
} packed_msg_t;

void packed_msg_init(packed_msg_t *m);
void packed_msg_free(packed_msg_t *m);
int  pm_has(const packed_msg_t *m, uint16_t flag);

/* Serialize one message (header + NUL-terminated fields) appended to out. */
int  packed_msg_serialize(const packed_msg_t *m, sbuf *out);

typedef struct {
    /* common Type-2 header (0x00..0x19 + zones) */
    uint16_t orig_node, dest_node;
    uint16_t year, month, day, hour, minute, second, baud;
    uint16_t orig_net, dest_net;
    uint8_t  prod_code_lo, prod_rev_major;
    uint8_t  password[FTN_PASSWORD_SIZE];
    uint16_t orig_zone, dest_zone;
    /* Type-2e / Type-2+ extended header (0x22..0x39) */
    uint16_t q_orig_zone, q_dest_zone;
    uint16_t aux_net;       /* filler (2e) / auxNet (2+) at 0x26 */
    uint16_t cap_valid;
    uint8_t  prod_code_hi, prod_rev_minor;
    uint16_t cap_word;
    uint16_t orig_point, dest_point;
    uint8_t  prod_data[FTN_PROD_DATA_SIZE];
    /* Type-2 fill region (0x26..0x39) */
    uint8_t  fill[FTN_FILL_SIZE];
    /* messages */
    packed_msg_t *messages;
    size_t msg_count, msg_cap;
    pkt_type_t type;
} packet_t;

void packet_init(packet_t *p);
void packet_free(packet_t *p);

/* Deep-copy append of a message; returns 0 ok, -1 oom. */
int  packet_add_msg(packet_t *p, const packed_msg_t *m);

/* File I/O; return 0 on success, -1 on error (errmsg filled). */
int  packet_read_file(const char *path, packet_t *out, char *errmsg, size_t errsz);
int  packet_write_file(const char *path, packet_t *p, char *errmsg, size_t errsz);

/* Serialize header + messages + 0x0000 terminator appended to out. */
int  packet_serialize(const packet_t *p, sbuf *out);

#endif /* FTNPKT_PACKET_H */
