#ifndef SENTINEL_CORE_H
#define SENTINEL_CORE_H
#include <stdint.h>
#ifdef SC_BUILDING_CORE
#define SC_API __declspec(dllexport)
#else
#define SC_API __declspec(dllimport)
#endif
#ifdef __cplusplus
extern "C" {
#endif
#define SC_ABI_VERSION 1u
#define SC_DIAGNOSTIC_LIMIT 16u
#define SC_CAP_INSPECTION UINT64_C(1)
#define SC_CAP_LIFECYCLE UINT64_C(2)
/* These are foundation capabilities only. No game capabilities exist in ABI 1. */
typedef uint32_t sc_result;
enum { SC_OK, SC_INVALID_ARGUMENT, SC_ABI_MISMATCH, SC_CAPABILITY_UNAVAILABLE,
       SC_BOOTSTRAP_FAILURE, SC_INSPECTION_FAILURE };
enum { SC_COLD, SC_READY, SC_STOPPED };
typedef struct sc_status {
    uint32_t size;
    uint32_t abi_version;
    uint64_t capabilities;
    uint32_t state;
    sc_result last_result;
    uint32_t initialization_count;
    uint32_t diagnostic_count;
    char version[32];
    char build_id[65];
} sc_status;
/* Callable outside DllMain only. Callers own valid buffers and quiesce all calls
   before shutdown/unload. ABI 1 requires the exact status size. No retained pointers.
   Initialization also starts local inspection. SC_READY describes Core initialization,
   not IPC/game readiness. Failed shutdown requires retaining the module and retrying. */
SC_API sc_result sc_inspect(uint32_t abi, uint32_t size, sc_status* status);
SC_API sc_result sc_initialize(uint32_t abi, uint64_t required_capabilities);
SC_API sc_result sc_shutdown(void);
#ifdef __cplusplus
}
#endif
#endif
