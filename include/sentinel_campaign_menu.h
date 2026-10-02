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
    uint32_t id, flags; // fixed caller id; zero is reserved
    uint32_t native_index; // mission entry supplied by the room
    char map[192], title[96];
} sc_campaign_row;
// sentinel.campaign_presentation.v1 accompanies a row at the same revision.
// known: zero for legacy callers, one for facts, two for unavailable facts.
// Native consumers own all UI objects.
typedef struct sc_physical_challenge {
    char unlockable[80];
    uint32_t found, required, checked;
} sc_physical_challenge;
typedef struct sc_campaign_summary {
    // band: tier 1..4 in low byte; high bytes hold centi-CR + 1 when known.
    // Zero keeps legacy/unknown presentation and the wire layout unchanged.
    uint32_t known, found, total, band;
    sc_physical_challenge challenges[3];
} sc_campaign_summary;

// room rewards use the same menu revision
enum { SC_REWARD_MISSION=1, SC_REWARD_AGGREGATE=2, SC_REWARD_MASTERY=3 };
typedef struct sc_campaign_reward {
    uint32_t location_id, kind, checked;
    char unlockable[80], name[128], text[512];
} sc_campaign_reward;
typedef struct sc_campaign_rewards {
    uint32_t known, count;
    sc_campaign_reward entries[17];
} sc_campaign_rewards;
typedef struct sc_campaign_request {
    sc_diagnostic_request execution;
    char namespace_id[65];
    uint64_t revision;
    uint32_t index, count;
    sc_campaign_row row;
    uint32_t reward_presentation;
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
    uint32_t intent_count, intent_ids[96];
} sc_campaign_result;
#endif
