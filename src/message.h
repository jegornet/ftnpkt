#ifndef FTNPKT_MESSAGE_H
#define FTNPKT_MESSAGE_H

#include <stddef.h>
#include <stdint.h>

#include "addr.h"
#include "packet.h"

#define MSG_ERR_MISSING_DEST "--to-addr is required when --areatag is not given (netmail needs a destination address)"
#define MSG_DEFAULT_ORIGIN   "ftnpkt"
#define MSG_DEFAULT_ATTR     0x0100u   /* Local */

typedef struct {
    char       *from_name;
    ftn_addr_t  from_addr;
    char       *to_name;
    ftn_addr_t  to_addr;
    int         has_to_addr;
    char       *subject;
    uint16_t    attr;
    char       *charset;
    char       *areatag;
    char       *msg_id;
    char       *tear_line;
    char       *origin;
    char       *seen_by;
    char       *path;
    char      **add_kludge;
    size_t      add_kludge_count;
} message_args_t;

void message_args_init(message_args_t *a);
void message_args_free(message_args_t *a);

/* Resolve (orig,dest). dest defaults to sender-without-point for echomail.
 * Returns 0 ok, -1 on error (errmsg). */
int resolve_addresses(const message_args_t *a, ftn_addr_t *orig, ftn_addr_t *dest,
                      char *errmsg, size_t errsz);

/* Build a PackedMsg from args + body text + addresses. `out` is initialized
 * afresh (caller frees with packed_msg_free). Returns 0 ok, -1 on error. */
int build_message_from_args(const message_args_t *a, const char *text,
                            ftn_addr_t orig, ftn_addr_t dest,
                            packed_msg_t *out, char *errmsg, size_t errsz);

#endif /* FTNPKT_MESSAGE_H */
