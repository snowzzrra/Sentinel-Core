// Copyright (c) 2026 snowzzrra. MIT; see ../LICENSE.
#pragma once

// offline synthetic-fixture storage commands only. returns -1 for other probe modes
int save_storage_command(int argc, wchar_t** argv);
int native_backup_command(int argc, wchar_t** argv);
