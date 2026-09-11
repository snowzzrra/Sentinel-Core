// Copyright (c) 2026 snowzzrra. MIT; see ../LICENSE.
#pragma once
#include "save_write.h"

namespace sentinel::save {
// Json::Value ABI for the selected executable. Comments stay on their original
// member when payload/type/string ownership are swapped by the native ABI.
struct ProfileValue { uint64_t payload; uint32_t type; uint8_t owned, padding[3]; uintptr_t comments; };
struct ProfileHolder { uint8_t direction, padding[7]; ProfileValue* root; };
using ReadProfile = uint64_t (*)(SaveReference*, SaveReference*);
using SerializeProfile = uint32_t (*)(uintptr_t, uintptr_t, ProfileHolder*);
using LookupProfileValue = ProfileValue* (*)(ProfileValue*, const char*);
using DestroyProfileValue = void (*)(ProfileValue*);
using ProfileChecksum = uint64_t (*)(const void*, uint64_t);
using PrepareProfile = void (*)(SaveReference*, SaveReference*, uintptr_t, const char*);
using RetainProfileReference = SaveReference* (*)(SaveReference*, const SaveReference*);
struct ProfileCalls {
    ReadProfile read;
    SerializeProfile serialize;
    LookupProfileValue lookup;
    DestroyProfileValue destroy;
    ProfileChecksum checksum;
    ReleaseSaveReference release;
    uintptr_t image_base;
};
uint64_t read_profile(Session&, engine::Memory&, SaveReference*, SaveReference*, const ProfileCalls&);
uint32_t serialize_profile(Session&, engine::Memory&, uintptr_t manager, uintptr_t profile,
    ProfileHolder*, const ProfileCalls&);
bool profile_payload_valid(Session&, engine::Memory&, uintptr_t data, const ProfileCalls&);
void prepare_profile_write(Session&, engine::Memory&, SaveReference* profile, SaveReference* data, uintptr_t shell,
    const char* suffix, PrepareProfile, RetainProfileReference, const ProfileCalls&);
} // namespace sentinel::save
