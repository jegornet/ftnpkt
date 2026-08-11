#ifndef FTNPKT_CHARSET_H
#define FTNPKT_CHARSET_H

#include <stddef.h>
#include <stdint.h>

#include "util.h"

/* Maps a FidoNet CHRS name (e.g. "CP866", "LATIN-1", "koi8-r") to a canonical
 * iconv codepage name, or returns NULL when unknown. Case- and alias-tolerant. */
const char *charset_canonical(const char *name);

/* True when name maps to a supported charset (UTF-8 or an 8-bit code page). */
int charset_is_known(const char *name);

/* Encode a UTF-8 string into the named charset, appending raw bytes to `out`.
 * Unencodable runes become '?'. Returns 0 on success, -1 if charset unknown. */
int charset_encode(const char *name, const char *utf8, sbuf *out);

/* Decode raw bytes from the named charset into a UTF-8 string in `out` (lossy,
 * invalid sequences -> U+FFFD). Returns 0 if known, -1 if charset unknown. */
int charset_decode(const char *name, const uint8_t *data, size_t len, sbuf *out);

#endif /* FTNPKT_CHARSET_H */
