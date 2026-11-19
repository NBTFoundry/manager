#ifndef NBT_MOST_H
#define NBT_MOST_H

/* MOSTSubSystem.DSYSMOSTServer 4.5 payloads as carried by DSI streams:
 * little-endian, each scalar aligned relative to the payload start. These
 * layouts are reconstructed target assumptions (see docs/controller-input.md). */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NBT_MOST_INTERFACE "DSYSMOSTServer"
#define NBT_MOST_ROLE "MOSTSubSystem"
#define NBT_MOST_MAJOR 4
#define NBT_MOST_MINOR 5

#define NBT_MOST_REQ_REGISTER_CONTROLS 0x1u
/* unregisterDevCtrls. Its payload is assumed to mirror request 0x1 (client id,
 * count, keys); unverified, so the GUI sends it only when configured to. */
#define NBT_MOST_REQ_UNREGISTER_CONTROLS 0x5u
#define NBT_MOST_REQ_REGISTER_CLIENT 0xfu
#define NBT_MOST_INFO_CLIENT 0x80000000u
#define NBT_MOST_INFO_MESSAGE 0x80000001u
#define NBT_MOST_CLIENT_REFUSED 0xffffffffu
/* Attribute Registry: the FBlocks present on the ring (already subscribed to). */
#define NBT_MOST_ATTR_REGISTRY 0xc0000001u

/* One RegistryEntry {u16, u16}. The library's own client collects `key`, the
 * second word, as the FBlock key (fblock << 8 | instance); the meaning of the
 * first word is not confirmed. */
typedef struct {
    uint16_t first;
    uint16_t key;
} nbt_most_registry_entry;

typedef struct {
    uint32_t tx_type;
    uint32_t variant_tag;
    uint32_t variant_value;
    uint16_t dst, src;
    uint8_t fblock, inst;
    uint16_t fkt;
    uint8_t op;
    uint32_t tag;              /* the header's u32 at offset 24 */
    const unsigned char *data; /* points into the parsed buffer */
    size_t length;
} nbt_most_message;

/* What a received message is, by the vendor client's own test: segments of a
 * longer message (variant 2, tx type 1, values 19-21), transmit status
 * reports (variant 2, values 4-9), or a message without transmission info
 * (variant tag other than 2, which the car does deliver). */
typedef enum {
    NBT_MOST_NORMAL,
    NBT_MOST_SEG_FIRST,
    NBT_MOST_SEG_MIDDLE,
    NBT_MOST_SEG_LAST,
    NBT_MOST_TX_STATUS,
    NBT_MOST_NO_INFO
} nbt_most_class;

#define NBT_MOST_REQ_PROCESS_MESSAGE 0x2u
#define NBT_MOST_ATTR_COMM_STATE 0xc0000000u
#define NBT_MOST_COMM_OK 2u
#define NBT_MOST_OP_COMMAND_MAX 8u /* ops 0-8 are commands, 9-15 replies */

/* Returns 0 for a complete, unsegmented message and -1 otherwise. */
int nbt_most_parse(const unsigned char *payload, size_t length, nbt_most_message *message);
/* Parses any well-formed message and returns its class, or -1. */
int nbt_most_parse_any(const unsigned char *payload, size_t length, nbt_most_message *message);
/* Request 0x2 (requestProcessMessage): client id, then a MessageType with
 * tx type 0 and variant {1 (TXControl), 0}, the form used for commands that
 * cars answered. Returns the payload length or -1. */
long nbt_most_process_message(uint32_t client, uint16_t src, uint16_t dst, uint8_t fblock,
                              uint8_t inst, uint16_t fkt, uint8_t op, uint32_t tag,
                              const unsigned char *data, size_t length, unsigned char *out,
                              size_t size);
/* Parses the registration reply with the client's device address (u16 after
 * the client id); -1 if malformed or refused. */
int nbt_most_client_reply(const unsigned char *payload, size_t length, uint32_t *client,
                          uint16_t *device);

/* Joins segmented messages as the vendor client does: by equal header (dst,
 * src, FBlock, instance, function, op, tag). Bounded: NBT_MOST_JOIN_SLOTS
 * pending messages of at most NBT_MOST_JOIN_MAX bytes; the oldest pending one
 * is dropped for a new first segment. Not thread safe; no allocation. */
#define NBT_MOST_JOIN_SLOTS 8
#define NBT_MOST_JOIN_MAX 4096u
typedef struct {
    int used;
    unsigned stamp;
    nbt_most_message head;
    size_t length;
    unsigned char data[NBT_MOST_JOIN_MAX];
} nbt_most_join_slot;
typedef struct {
    unsigned clock;
    unsigned lost;
    nbt_most_join_slot slots[NBT_MOST_JOIN_SLOTS];
} nbt_most_joiner;
void nbt_most_join_init(nbt_most_joiner *joiner);
/* Feeds one segment. Returns 1 with `out` the whole message (its data valid
 * until the next call), 0 while pending, -1 when the segment is lost (no
 * first segment, or over the size bound). */
int nbt_most_join(nbt_most_joiner *joiner, const nbt_most_message *segment, nbt_most_class kind,
                  nbt_most_message *out);
/* Parses the Registry attribute: an int count, then that many entries of two
 * u16 (DSYSMOSTTypes::DSIReadRegistry). Returns the count and stores the first
 * `max` entries, or returns -1 if the payload is malformed (negative count or
 * entries beyond the payload). */
long nbt_most_registry(const unsigned char *payload, size_t length,
                       nbt_most_registry_entry *entries, size_t max);
/* Parses the registration reply; returns -1 if malformed or refused. */
int nbt_most_client_id(const unsigned char *payload, size_t length, uint32_t *client);
/* Builders return the payload length, or -1 if `size` is too small or input is invalid. */
long nbt_most_register_client(const char *name, unsigned char *out, size_t size);
long nbt_most_register_controls(uint32_t client, const uint16_t *keys, size_t count,
                                unsigned char *out, size_t size);
long nbt_most_unregister_controls(uint32_t client, const uint16_t *keys, size_t count,
                                  unsigned char *out, size_t size);
/* Attribute and information ids the HMI's own MOST client subscribes to. */
extern const uint32_t nbt_most_subscriptions[];
extern const size_t nbt_most_subscription_count;

#ifdef __cplusplus
}
#endif
#endif
