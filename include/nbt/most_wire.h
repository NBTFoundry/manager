#ifndef NBT_MOST_WIRE_H
#define NBT_MOST_WIRE_H

/* Bridge capability `most`: datagrams between an application and its host on
 * the AF_UNIX SOCK_DGRAM socket NBT_MOST_FD. One datagram is one record;
 * every record starts with {u8 version = 1, u8 type, u8, u8}. Little-endian.
 * docs/most-bridge.md is the contract; this C99 module has no dependencies
 * beyond the C library, so applications can compile it in unchanged. */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NBT_MOST_WIRE_VERSION 1
#define NBT_MOST_WIRE_HEADER 24u         /* MESSAGE and SEND, before data */
#define NBT_MOST_WIRE_DATA_MAX 4096u     /* MOST payload bytes per record */
#define NBT_MOST_WIRE_REGISTRY_MAX 1000u /* entries per REGISTRY record */
/* The largest record either side sends (a full MESSAGE; a full REGISTRY is
 * 4008 bytes); size receive buffers with it. */
#define NBT_MOST_WIRE_RECORD_MAX (NBT_MOST_WIRE_HEADER + NBT_MOST_WIRE_DATA_MAX)

/* Record types. Host to application: */
#define NBT_MOST_WIRE_STATE 0x01
#define NBT_MOST_WIRE_REGISTRY 0x02
#define NBT_MOST_WIRE_MESSAGE 0x03
#define NBT_MOST_WIRE_RESULT 0x04
/* Application to host: */
#define NBT_MOST_WIRE_SEND 0x81

/* Link states in STATE. */
#define NBT_MOST_LINK_DETACHED 0    /* no connection to the MOST server */
#define NBT_MOST_LINK_REGISTERING 1 /* connected, registration not answered */
#define NBT_MOST_LINK_REFUSED 2     /* the server refused the registration */
#define NBT_MOST_LINK_COMM_LOST 3   /* registered; communication was OK and is not */
#define NBT_MOST_LINK_UP 4          /* registered; sending is possible */

/* MESSAGE flags. */
#define NBT_MOST_FLAG_JOINED 0x01  /* joined by the host from segments */
#define NBT_MOST_FLAG_NO_INFO 0x02 /* delivered without transmission info */

/* RESULT codes. */
#define NBT_MOST_SENT 0           /* handed to the MOST server */
#define NBT_MOST_NOT_ALLOWED 1    /* not a tx function of the package */
#define NBT_MOST_BAD_REQUEST 2    /* malformed, or op above 8 */
#define NBT_MOST_NOT_UP 3         /* link not up */
#define NBT_MOST_NO_DESTINATION 4 /* no ring node hosts the FBlock (registry) */
#define NBT_MOST_SEND_FAILED 5    /* the server connection refused the request */
#define NBT_MOST_BUSY 6           /* over the send rate limit; try again later */

typedef struct {
    uint8_t link;     /* NBT_MOST_LINK_* */
    uint16_t address; /* this client's MOST address while up, else 0 */
    uint32_t dropped; /* records the host could not deliver so far */
} nbt_most_wire_state;

typedef struct {
    uint16_t address; /* device address */
    uint16_t key;     /* fblock << 8 | instance */
} nbt_most_wire_entry;

typedef struct {
    uint16_t src, dst;
    uint8_t fblock, inst;
    uint16_t fkt;     /* 12 bits */
    uint8_t op;       /* 0-15 */
    uint8_t flags;    /* NBT_MOST_FLAG_* (MESSAGE only) */
    uint32_t tag;     /* the MOST header's u32 tag */
    uint32_t dropped; /* MESSAGE: as in STATE */
    uint32_t request; /* SEND: echoed in RESULT */
    const uint8_t *data;
    size_t length;
} nbt_most_wire_message;

typedef struct {
    uint32_t request;
    uint8_t code; /* NBT_MOST_SENT, ... */
} nbt_most_wire_result;

/* Returns the record type of a received datagram, or -1 if its version or
 * length is wrong. Then decode with the matching function (0 or -1). */
int nbt_most_wire_type(const uint8_t *record, size_t length);
int nbt_most_wire_decode_state(const uint8_t *record, size_t length, nbt_most_wire_state *out);
/* Stores at most `max` entries; returns the count in the record, or -1. */
long nbt_most_wire_decode_registry(const uint8_t *record, size_t length,
                                   nbt_most_wire_entry *entries, size_t max);
/* MESSAGE or SEND. `data` points into `record`. */
int nbt_most_wire_decode_message(const uint8_t *record, size_t length, nbt_most_wire_message *out);
int nbt_most_wire_decode_result(const uint8_t *record, size_t length, nbt_most_wire_result *out);

/* Encoders return the record length, or -1 if `size` is too small or a value
 * is out of range. `type` is NBT_MOST_WIRE_MESSAGE or NBT_MOST_WIRE_SEND. */
long nbt_most_wire_encode_state(const nbt_most_wire_state *state, uint8_t *out, size_t size);
long nbt_most_wire_encode_registry(const nbt_most_wire_entry *entries, size_t count, uint8_t *out,
                                   size_t size);
long nbt_most_wire_encode_message(int type, const nbt_most_wire_message *message, uint8_t *out,
                                  size_t size);
long nbt_most_wire_encode_result(const nbt_most_wire_result *result, uint8_t *out, size_t size);

#ifdef __cplusplus
}
#endif
#endif
