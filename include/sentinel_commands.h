#ifndef SENTINEL_COMMANDS_H
#define SENTINEL_COMMANDS_H
#include "sentinel_native.h"

#define SC_COMMAND_ABI_VERSION 1u
#define SC_COMMAND_PROCESS_NAMESPACE "0000000000000000000000000000000000000000000000000000000000000000"
#define SC_COMMAND_TEXT_CAPACITY 2048u
enum {
    SC_COMMAND_ACTIVATE = 1,
    SC_COMMAND_PLAYER_PERK = 2,
    SC_COMMAND_AMMO_REFILL = 3,
    SC_COMMAND_CRUCIBLE_REFILL = 4,
    SC_COMMAND_ECHO = 5,
    SC_COMMAND_CONDUMP = 6,
    SC_COMMAND_LIST_INVENTORY = 7,
    SC_COMMAND_TRANSIENT_POLICY = 8
};
typedef struct sc_command_request {
    sc_diagnostic_request execution;
    char namespace_id[65];
    uint32_t kind;
    /* One bounded operation matching kind; this is not a remote console. */
    char text[SC_COMMAND_TEXT_CAPACITY];
} sc_command_request;
enum {
    SC_COMMAND_NOT_EXECUTED = 0,
    /* Native callback returned. Gameplay/persistence require independent proof. */
    SC_COMMAND_DISPATCHED = 1,
    SC_COMMAND_UNAVAILABLE = 2,
    SC_COMMAND_NO_PLAYER = 3,
    SC_COMMAND_NATIVE_FAILED = 4
};
typedef struct sc_command_result {
    uint32_t size, abi_version;
    sc_diagnostic_result execution;
    char namespace_id[65];
    uint32_t kind, outcome, native_exception;
} sc_command_result;
#endif
