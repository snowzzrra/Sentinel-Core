#pragma once
#include "sentinel_native.h"
// AP Location IDs, not Item IDs. Complete room snapshots replace revisions.
// Known snapshots must retain previously accepted checks in this Save namespace,
// matching MultiServer.register_location_checks. Regressions are refused, never
// silently unioned. known=0 suspends projection; it does not clear that history.
// ACCEPTED acknowledges copied input, not native convergence or save persistence.
#define SC_AUTOMAP_ABI_VERSION 1u
#define SC_AUTOMAP_LOCATION_BASE 7770000u
#define SC_AUTOMAP_LOCATION_BITS 512u
enum { SC_AUTOMAP_PUBLISH=1, SC_AUTOMAP_OBSERVE=2 };
enum { SC_AUTOMAP_ACCEPTED, SC_AUTOMAP_REFUSED, SC_AUTOMAP_UNAVAILABLE, SC_AUTOMAP_REGRESSION };
typedef struct sc_automap_request {
    sc_diagnostic_request execution;
    char namespace_id[65];
    uint32_t kind, known;
    uint64_t revision, checked_locations[8];
} sc_automap_request;
typedef struct sc_automap_result {
    uint32_t size, abi_version;
    sc_native_scope scope;
    uint64_t request_id;
    uint8_t nonce[16];
    char namespace_id[65];
    uint32_t kind, outcome, known, native_fault;
    uint64_t revision, scanned, removed, completed_passes;
} sc_automap_result;
