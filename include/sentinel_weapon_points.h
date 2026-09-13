#ifndef SENTINEL_WEAPON_POINTS_H
#define SENTINEL_WEAPON_POINTS_H
#include "sentinel_native.h"

/* Independent typed capability. Existing Core/native/wire ABIs are unchanged.
   No currency selector, native pointer, AP item ID or receipt history crosses it. */
#define SC_WEAPON_POINTS_ABI_VERSION 1u
#define SC_WEAPON_POINTS_MAX_GRANT 117u
enum { SC_WUP_OBSERVE, SC_WUP_GRANT };
typedef struct sc_weapon_points_request {
    sc_diagnostic_request execution;
    char namespace_id[65];
    uint32_t kind, amount, expected_gained;
} sc_weapon_points_request;
enum {
    SC_WUP_NOT_EXECUTED, SC_WUP_OBSERVED, SC_WUP_GRANTED,
    SC_WUP_PRECONDITION, SC_WUP_NO_PLAYER, SC_WUP_READ_FAILED,
    SC_WUP_NATIVE_FAILED, SC_WUP_REFRESH_FAILED, SC_WUP_UNAVAILABLE
};
enum { SC_WUP_BEFORE_VALID = 1, SC_WUP_AFTER_VALID = 2, SC_WUP_NATIVE_ENTERED = 4,
    SC_WUP_REFRESHED = 8, SC_WUP_SUPPRESSION_ACTIVE = 16 };
typedef struct sc_weapon_points_result {
    uint32_t size, abi_version;
    sc_diagnostic_result execution;
    char namespace_id[65];
    uint32_t kind, amount, expected_gained;
    uint32_t outcome, flags, native_exception;
    uint32_t balance_before, balance_after, gained_before, gained_after;
    uint64_t suppressed_grants;
} sc_weapon_points_result;
/* EXECUTED confirms dispatch only. GRANTED + exact before/after facts confirms
   native mutation. A timeout or missing retained result never proves no grant.
   Gained is native cumulative positive currency, not current spendable balance.
   After consuming a terminal result the caller explicitly releases its queue
   slot. Release never cancels queued/claimed execution. Subsequent retrieval is
   UNKNOWN; AP ownership remains in the caller's already durable grant intent. */
#endif
