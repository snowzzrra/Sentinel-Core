#ifndef SENTINEL_EVIDENCE_H
#define SENTINEL_EVIDENCE_H
#include "sentinel_native.h"

typedef struct sc_evidence_scope {
    sc_native_scope process;
    char namespace_id[65];
    uint8_t save_binding[16];
    char map_name[192];
    uint32_t readiness;
} sc_evidence_scope;
enum { SC_EVIDENCE_UNPROVEN, SC_EVIDENCE_READY };
enum { SC_CURRENCY_OBSERVE, SC_CURRENCY_GRANT };
enum { SC_CURRENCY_BEFORE_VALID = 1, SC_CURRENCY_AFTER_VALID = 2, SC_CURRENCY_WRITER_ENTERED = 4 };
enum { SC_CURRENCY_NOT_EXECUTED, SC_CURRENCY_OBSERVED, SC_CURRENCY_GRANTED,
    SC_CURRENCY_PRECONDITION, SC_CURRENCY_READ_FAILED, SC_CURRENCY_UNKNOWN };
typedef struct sc_currency_request {
    sc_diagnostic_request execution;
    char namespace_id[65];
    uint32_t kind, currency, amount, expected_gained;
} sc_currency_request;
typedef struct sc_currency_result {
    sc_diagnostic_result execution;
    sc_evidence_scope before_scope, after_scope;
    uint32_t kind, currency, amount, expected_gained;
    uint32_t outcome, flags, native_exception;
    uint32_t balance_before, gained_before, balance_after, gained_after;
} sc_currency_result;
enum { SC_EFFECT_UNPROVEN, SC_EFFECT_PRESENT, SC_EFFECT_ABSENT };
typedef struct sc_progression_request {
    sc_diagnostic_request execution;
    char namespace_id[65];
    uint32_t kind;
} sc_progression_request;
typedef struct sc_progression_result {
    sc_diagnostic_result execution;
    sc_evidence_scope before_scope, after_scope;
    uint32_t suit_known, suit_owned;
    int32_t slayer_key_quantity;
    uint32_t meat_hook;
    uint8_t mastery_proof[13];
} sc_progression_result;
typedef struct sc_mission_access_request {
    sc_diagnostic_request execution;
    char namespace_id[65];
    uint32_t kind, stage_id;
    uint8_t save_binding[16];
} sc_mission_access_request;
typedef struct sc_mission_access_result {
    sc_diagnostic_result execution;
    sc_evidence_scope menu_scope, destination_scope;
    uint64_t revision, destination_revision;
    uint32_t stage_id, destination_stage_id;
    char map_name[192];
    uint32_t entry_usable, destination_ready;
} sc_mission_access_result;
#endif
