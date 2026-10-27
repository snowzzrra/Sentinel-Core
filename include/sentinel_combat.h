#ifndef SENTINEL_COMBAT_H
#define SENTINEL_COMBAT_H
#include "sentinel_native.h"

#define SC_COMBAT_ABI_VERSION 2u
#define SC_COMBAT_MAX_MAPS 64u
#define SC_COMBAT_PRESENTATION_ABI_VERSION 1u
enum { SC_COMBAT_ORIGIN_UNMARKED, SC_COMBAT_ORIGIN_CORE_REQUEST };

typedef struct sc_combat_query {
    char namespace_id[65];
    uint32_t map_count;
    char maps[SC_COMBAT_MAX_MAPS][SC_CONTEXT_MAP_CAPACITY];
} sc_combat_query;

typedef struct sc_combat_snapshot {
    uint32_t size, abi_version;
    sc_native_scope scope;
    uint32_t availability, reason;
    uint64_t observation_gaps;
    char namespace_id[65];
    uint64_t qualified_kills;
} sc_combat_snapshot;

typedef struct sc_combat_presentation_request {
    sc_diagnostic_request execution;
    char namespace_id[65];
    uint64_t total, target, first_milestone, milestone, milestone_step;
    uint32_t visible;
} sc_combat_presentation_request;

typedef struct sc_combat_presentation_result {
    uint32_t size, abi_version;
    sc_native_scope scope;
    uint64_t request_id;
    uint8_t nonce[16];
    uint32_t status, reason;
    uint64_t total, target, milestone;
    uint32_t visible;
} sc_combat_presentation_result;

#ifdef __cplusplus
extern "C" {
#endif
SC_API sc_result sc_combat_inspect(uint32_t abi_version, uint32_t size,
                                 const sc_combat_query* query, sc_combat_snapshot* snapshot);
#ifdef __cplusplus
}
#endif
#endif
