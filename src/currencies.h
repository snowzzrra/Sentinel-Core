#pragma once
#include "sentinel_evidence.h"

namespace sentinel::currencies {
struct Calls {
    void* context = nullptr;
    uintptr_t (*player)(void*) = nullptr;
    bool (*read)(void*, uintptr_t, uint32_t, uint32_t&, uint32_t&) = nullptr;
    uint32_t (*grant)(void*, uintptr_t, uint32_t, uint32_t, bool&) = nullptr;
};
bool valid(const sc_currency_request&);
sc_currency_result initial(const sc_currency_request&);
void execute(const sc_currency_request&, sc_currency_result&, const Calls&);
bool exact(const sc_currency_result&);
}
