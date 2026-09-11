// Copyright (c) 2026 snowzzrra. MIT; see ../LICENSE.
#pragma once
#include "save_write.h"
#include "save_collector.h"

namespace sentinel::save {
using EnumerateProvider = SaveFuture** (*)(uintptr_t, SaveFuture**, uintptr_t, SaveReference*, const char*);
using RetainSaveReference = SaveReference* (*)(SaveReference*, const SaveReference*);
struct CatalogCalls {
    EnumerateProvider enumerate;
    RetainSaveReference retain;
    ReleaseSaveReference release;
    SetSaveName set_name;
    AssignString assign;
    uintptr_t image_base;
};
bool read_campaign_prefix(engine::Memory&, uintptr_t image_base, std::string&);
// Wraps the real provider future. Terminal metadata is checked and normalized
// before the existing native manager continuation can import its slot cache.
SaveFuture** enumerate_provider(Session&, engine::Memory&, uintptr_t provider, SaveFuture**,
    uintptr_t identity, SaveReference*, const char* prefix, const CatalogCalls&);
} // namespace sentinel::save
