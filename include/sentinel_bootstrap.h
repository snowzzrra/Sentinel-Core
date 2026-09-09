#ifndef SENTINEL_BOOTSTRAP_H
#define SENTINEL_BOOTSTRAP_H
#include "sentinel_core.h"
#ifdef __cplusplus
extern "C" {
#endif
enum { SC_BOOT_STARTING, SC_BOOT_READY, SC_BOOT_FAILED, SC_BOOT_STOPPED };
typedef struct sc_boot_status {
    uint32_t size;
    uint32_t state;
    uint32_t win32_error;
    sc_status core;
} sc_boot_status;
/* Auto-starts once at DLL attach. Do not call from DllMain. Keep the DLL loaded
   until sc_bootstrap_shutdown succeeds; concurrent unload is not supported. */
sc_result sc_bootstrap_inspect(uint32_t abi, uint32_t size, sc_boot_status* status);
sc_result sc_bootstrap_shutdown(void);
#ifdef __cplusplus
}
#endif
#endif
