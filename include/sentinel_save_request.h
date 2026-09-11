#ifndef SENTINEL_SAVE_REQUEST_H
#define SENTINEL_SAVE_REQUEST_H
#include "sentinel_native.h"

/* Explicit backup of the currently selected owned campaign slot. These fields
   constrain the native submission; they do not select a slot or accept a path. */
#define SC_SAVE_BACKUP_ABI_VERSION 1u
#define SC_SAVE_BACKUP_MAX_WORK_MS 60000u
typedef struct sc_save_backup_request {
    sc_diagnostic_request execution;
    char namespace_id[65]; /* Complete lowercase SHA256 namespace identity. */
    uint32_t campaign; /* 0 GAME, 1 DLC1, 2 DLC2. */
    uint32_t slot; /* 0..11; must match the actual native writer. */
    uint32_t work_deadline_ms; /* Checked before copy and between chunks. */
} sc_save_backup_request;

enum {
    SC_BACKUP_UNKNOWN, SC_BACKUP_QUEUED, SC_BACKUP_CLAIMED,
    SC_BACKUP_WAITING_NATIVE, SC_BACKUP_COPYING, SC_BACKUP_COPIED,
    SC_BACKUP_COMPLETE, SC_BACKUP_FAILED, SC_BACKUP_REJECTED,
    SC_BACKUP_CANCELLED, SC_BACKUP_EXPIRED
};
enum {
    SC_BACKUP_FAILURE_NONE, SC_BACKUP_FAILURE_CANCELLED,
    SC_BACKUP_FAILURE_DEADLINE, SC_BACKUP_FAILURE_NATIVE, SC_BACKUP_FAILURE_STORAGE
};
enum {
    SC_BACKUP_NATIVE_ENTERED = 1, SC_BACKUP_TASK_RETURNED = 2,
    SC_BACKUP_SOURCE_MATCHED = 4, SC_BACKUP_CANCEL_REQUESTED = 8,
    SC_BACKUP_READ_TERMINAL = 16, SC_BACKUP_READ_SUCCESS = 32,
    SC_BACKUP_STORAGE_ATTEMPTED = 64, SC_BACKUP_STORAGE_COMPLETE = 128
};
typedef struct sc_save_backup_snapshot {
    uint32_t size, abi_version, state, failure;
    /* Queue/execution facts retain their original meanings. EXECUTED here means
       the native submission ran; only state COMPLETE confirms the whole backup. */
    sc_diagnostic_result execution;
    uint64_t operation_id;
    uint32_t flags, native_exception, storage_outcome, storage_error;
    uint32_t files;
    uint64_t bytes;
    char basename[80]; /* Generated local attempt name; empty before output. */
    char namespace_id[65];
    uint32_t campaign, slot;
} sc_save_backup_snapshot;
/* UNKNOWN after retention/disconnect never proves nonexecution. COMPLETE means
   correlated native write/readback plus a verified local transport archive, not
   playable reopen, global idle or cloud synchronization. Partial output survives
   cancellation/failure. No payloads or native pointers are exposed. */
#endif
