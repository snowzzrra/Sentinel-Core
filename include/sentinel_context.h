#ifndef SENTINEL_CONTEXT_H
#define SENTINEL_CONTEXT_H
#include "sentinel_engine.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Independent, exact-size C contract. Existing Core/engine ABI 1 stays unchanged. */
#define SC_CONTEXT_ABI_VERSION 1u
#define SC_CONTEXT_MAP_CAPACITY 256u
#define SC_CONTEXT_FIELD_COUNT 5u
enum { SC_CONTEXT_GAME_STATE, SC_CONTEXT_STATE_CHANGED_MS, SC_CONTEXT_LOAD_SERIAL,
       SC_CONTEXT_CUTSCENE_ACTIVE, SC_CONTEXT_ENTRY_MODE };
enum { SC_GAME_MAIN_MENU, SC_GAME_LOADING, SC_GAME_IN_GAME };
/* Context-only reasons; never emitted in the frozen engine operation. */
enum { SC_CONTEXT_UNSUPPORTED = 18, SC_CONTEXT_MENU_WORLD,
       SC_CONTEXT_NAME_TOO_LONG, SC_CONTEXT_EMPTY_NAME };

typedef struct sc_context_field {
    uint32_t validity;
    uint32_t reason;
    uint64_t value;
    uint32_t win32_error;
} sc_context_field;
typedef struct sc_context_map {
    uint32_t validity;
    uint32_t reason;
    uint32_t win32_error;
    uint32_t length; /* Bytes excluding NUL; zero when unknown. No truncation. */
    char bytes[SC_CONTEXT_MAP_CAPACITY]; /* Owned exact native ASCII path. */
} sc_context_map;
typedef struct sc_context_snapshot {
    uint32_t size;
    uint32_t abi_version;
    uint32_t pid;
    uint64_t process_created;
    uint8_t instance[16]; /* Core lifetime, not a map/load identity. */
    uint64_t sequence;
    uint64_t sampled_at_ms;
    uint32_t duration_ms;
    uint32_t sample_reason;
    uint32_t profile;
    uint32_t locator_revision;
    uint32_t layout_revision;
    uint32_t root_locator_reason;
    uint32_t disk_hash_reason;
    char disk_sha256[65];
    sc_context_map current_map;
    sc_context_field fields[SC_CONTEXT_FIELD_COUNT];
} sc_context_snapshot;

/* Cached, bounded observation only. current_map requires native IN_GAME state;
   this is not player control, transport health or authorization to execute actions.
   STATE_CHANGED_MS is a wrapping engine clock stamp, never a load serial.
   LOAD_SERIAL, CUTSCENE_ACTIVE and ENTRY_MODE remain explicitly unsupported. */
SC_API sc_result sc_context_inspect(uint32_t abi, uint32_t size, sc_context_snapshot* snapshot);
#ifdef __cplusplus
}
#endif
#endif
