#include "sentinel_bootstrap.h"
#include "sentinel_engine.h"
#include "sentinel_context.h"
#include "sentinel_save.h"
#include "sentinel_save_request.h"
/* Compile the public inspection contract as C as well as C++. */
int sentinel_c_status_size(void) { return (int)sizeof(sc_status); }
int sentinel_c_engine_size(void) { return (int)sizeof(sc_engine_snapshot); }
int sentinel_c_context_size(void) { return (int)sizeof(sc_context_snapshot); }
int sentinel_c_save_size(void) { return (int)sizeof(sc_save_snapshot); }
int sentinel_c_save_admission_size(void) { return (int)sizeof(sc_save_admission_snapshot); }
int sentinel_c_save_write_size(void) { return (int)sizeof(sc_save_write_snapshot); }
int sentinel_c_backup_request_size(void) { return (int)sizeof(sc_save_backup_request); }
int sentinel_c_backup_snapshot_size(void) { return (int)sizeof(sc_save_backup_snapshot); }
