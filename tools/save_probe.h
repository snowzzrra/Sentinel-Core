// Copyright (c) 2026 snowzzrra. MIT; see ../LICENSE.
#pragma once

// Offline synthetic-fixture storage commands only. Returns -1 for other probe modes.
int save_storage_command(int argc, wchar_t** argv);
int native_backup_command(int argc, wchar_t** argv);
