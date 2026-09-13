#ifndef SENTINEL_CAMPAIGN_MENU_H
#define SENTINEL_CAMPAIGN_MENU_H
#include "sentinel_native.h"

// sentinel.campaign_menu.v1: caller-owned presentation, never AP progression.
// Committed is an admission fact. Rendered/loaded require native observations.
#define SC_CAMPAIGN_MENU_ABI_VERSION 1u
#define SC_CAMPAIGN_MENU_MAX_ROWS 21u
enum { SC_CAMPAIGN_REVEALED=1, SC_CAMPAIGN_UNLOCKED=2, SC_CAMPAIGN_COMPLETED=4,
       SC_CAMPAIGN_GOAL=8, SC_CAMPAIGN_HUB=16, SC_CAMPAIGN_DETAILS=32 };
enum { SC_CAMPAIGN_ACCEPTED=0, SC_CAMPAIGN_REFUSED=1 };
enum { SC_CAMPAIGN_OK=0, SC_CAMPAIGN_SCOPE=1, SC_CAMPAIGN_REVISION=2,
       SC_CAMPAIGN_INCOMPLETE=3, SC_CAMPAIGN_ROWS=4, SC_CAMPAIGN_NATIVE=5 };
typedef struct sc_campaign_row {
    uint32_t id, flags; // Opaque stable caller identity; zero is reserved.
    uint32_t native_index; // Entry in the room-authored native mission roster.
    char map[192], title[96];
} sc_campaign_row;
typedef struct sc_campaign_request {
    sc_diagnostic_request execution;
    char namespace_id[65];
    uint64_t revision;
    uint32_t index, count;
    sc_campaign_row row;
} sc_campaign_request;
typedef struct sc_campaign_result {
    uint32_t size, abi_version;
    sc_native_scope scope;
    uint64_t request_id;
    uint8_t nonce[16];
    char namespace_id[65];
    uint32_t status, reason;
    uint64_t committed_revision, rendered_revision;
    uint32_t selected_id, loaded_id;
} sc_campaign_result;
#endif
