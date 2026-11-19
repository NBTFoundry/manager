/* Wire/shared-memory constants and types for preloaded input capture. */
#ifndef NBT_CAPTURE_PROTOCOL_H
#define NBT_CAPTURE_PROTOCOL_H

#include "nbt/ccs.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NBT_CAPTURE_CONTROL_VERSION 1u
/* Next to iDrive's environment file, so neither can exist without the other
 * after a firmware update of /opt (docs/hmi-restart.md). */
#define NBT_CAPTURE_SHIM_PATH "/var/opt/nbt-manager/state/manager/libnbt-capture.so"
/* Where dev91-dev103 kept it; recognised so it can be moved and removed. */
#define NBT_CAPTURE_LEGACY_SHIM_PATH "/opt/hmi/ID5/data/ro/NbtCapture/libnbt-capture.so"
#define NBT_CAPTURE_CONTROL_PATH "/dev/shmem/nbt-manager-capture"
#define NBT_CAPTURE_LIVENESS_PATH "/dev/shmem/nbt-manager-gui-live"
#define NBT_CAPTURE_DIAG_PATH "/dev/shmem/nbt-manager-capture-diag"
#define NBT_CAPTURE_DSI_BASE_LIB "libsys_dsi_base.gpp.so.4"
/* iDrive's environment file (starter.cfg <EnvFile>): while it exists, the
 * process manager starts iDrive with the shim. The shim deletes it as soon as
 * it is loaded into iDrive, so each arming covers one start
 * (docs/hmi-restart.md). */
#define NBT_CAPTURE_HMI_ENV_PATH "/var/opt/nbt-manager/state/manager/hmi.env"

#define NBT_CAPTURE_CACHE_MS 100u
#define NBT_CAPTURE_DEFAULT_WATCHDOG_MS 1500u
#define NBT_CAPTURE_MIN_WATCHDOG_MS 500u
/* A hung or dead GUI gives iDrive its controller back within 1.5 s, always:
 * the shim refuses any record asking for longer (installs included). */
#define NBT_CAPTURE_MAX_WATCHDOG_MS 1500u
#define NBT_CAPTURE_ESCAPE_HOLD_MS 5000u

#define NBT_CAPTURE_CONTROL_MAX 256u
#define NBT_CAPTURE_DIAG_MAGIC 0x4E425444u /* "NBTD" */
#define NBT_CAPTURE_DIAG_VERSION 1u
#define NBT_CAPTURE_DIAG_SLOTS 32u

/* HmiMain3D's CPxyCECenterStack (FBlock 0x15) registers C01 volume button,
 * C02 favourite keys, C03-C07 AM/FM, Audio, Eject, Mode, Skip; only C02 is
 * held back (docs/input-capture.md). */
#define NBT_CAPTURE_FBLOCK_CENTER_STACK 0x15u
#define NBT_CAPTURE_FKT_FAVOURITES 0x0C02u

#define NBT_CAPTURE_OFF 0u
#define NBT_CAPTURE_ON 1u

typedef enum {
    NBT_CAP_DECISION_FORWARD = 0,
    NBT_CAP_DECISION_SUPPRESS = 1,
    NBT_CAP_DECISION_ESCAPE = 2
} nbt_capture_decision;

typedef enum {
    NBT_CAPTURE_DIAG_INIT = 1,
    NBT_CAPTURE_DIAG_OBSERVE = 2,
    NBT_CAPTURE_DIAG_SUPPRESS = 3,
    NBT_CAPTURE_DIAG_ESCAPE = 4,
    NBT_CAPTURE_DIAG_EXPIRED = 5,
    /* The real DSIClientDataResponse was not found: nothing can be forwarded. */
    NBT_CAPTURE_DIAG_UNRESOLVED = 6,
    /* A BACK hold stopped counting toward the escape: capture went off or a
     * new capture generation began before its release was seen. */
    NBT_CAPTURE_DIAG_HOLD_DROPPED = 7
} nbt_capture_diag_kind;

/* Versioned bounded control record written by GUI and read by shim. */
typedef struct {
    uint32_t version;
    int32_t gui_pid;
    uint64_t session_id;
    uint64_t generation;
    uint8_t state; /* NBT_CAPTURE_OFF or NBT_CAPTURE_ON */
    uint8_t knob_instance;
    uint8_t ccs_instance;
    uint8_t instances[NBT_CCS_INSTANCES_MAX];
    size_t instance_count;
    uint8_t knob_back_slot;
    uint8_t ccs_back_slot;
    uint32_t watchdog_ms;
    uint8_t escape_enabled;
} nbt_capture_control;

/* Non-destructive view of libsys_dsi_base's 64-byte C stream struct. */
typedef struct {
    uint32_t capacity;
    uint32_t populated;
    uint32_t channel;
    uint32_t read_pos;
    const unsigned char *buffer;
    uint32_t error_flag;
    uint32_t reserved[7];
    uint32_t flags;
} nbt_dsi_c_stream;

typedef struct {
    uint8_t is_candidate;
    uint8_t has_most;
    uint8_t fblock;
    uint8_t inst;
    uint16_t fkt;
    uint8_t op;
    uint8_t has_hard_back;
    uint8_t hard_back_held;
} nbt_capture_packet;

/* Lock-free shared-memory diagnostic ring slot (32 bytes). */
typedef struct {
    volatile uint32_t seq;
    uint8_t kind;
    uint8_t inst;
    uint8_t op;
    uint8_t flags;
    uint64_t timestamp_ms;
    uint64_t generation;
    uint16_t fkt;
    uint8_t fblock; /* 0 from shims before dev115 (then "reserved") */
    uint8_t detail; /* escape, dropped hold: BACK held, 100 ms units, 255 = 25.5 s or more */
    uint32_t seq_end;
} nbt_capture_diag_slot;

typedef struct {
    volatile uint32_t magic;
    volatile uint32_t version;
    volatile uint8_t observer_mode;
    uint8_t reserved[3];
    volatile int32_t pid;
    volatile uint32_t write_seq;
    volatile uint32_t dropped;
    nbt_capture_diag_slot slots[NBT_CAPTURE_DIAG_SLOTS];
} nbt_capture_diag_shm;

#ifdef __cplusplus
}
#endif
#endif
