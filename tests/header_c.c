#include "sentinel_bootstrap.h"
#include "sentinel_engine.h"
#include "sentinel_context.h"
/* Compile the public inspection contract as C as well as C++. */
int sentinel_c_status_size(void) { return (int)sizeof(sc_status); }
int sentinel_c_engine_size(void) { return (int)sizeof(sc_engine_snapshot); }
int sentinel_c_context_size(void) { return (int)sizeof(sc_context_snapshot); }
