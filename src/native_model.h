#pragma once
#include "sentinel_native.h"
#include <array>
#include <atomic>

namespace sentinel::native {
bool same_scope(const sc_native_scope& a, const sc_native_scope& b);

// Caller serializes lifecycle/admission/history; no lock survives an original
// engine call. A claimed slot belongs exclusively to its callback until the
// terminal release-store, so readers cannot observe a partly written result.
struct Lifecycle {
    uint64_t generation = 0, sequence = 0, overwritten = 0;
    uint32_t state = SC_LIFETIME_UNOBSERVED, depth = 0;
    uint32_t checkpoint_known = 0, checkpoint = 0;
    std::array<sc_native_event, SC_NATIVE_HISTORY> history{};
    void event(uint32_t kind, uint64_t now, uint32_t tid);
    void begin(bool change, bool flag_known, bool flag, uint64_t now, uint32_t tid);
    void end(bool change, bool success, uint32_t game_state, uint64_t now, uint32_t tid);
    void page(sc_native_snapshot& out, uint64_t after) const;
};

class Diagnostics {
public:
    struct Slot {
        std::atomic<uint32_t> state{SC_DIAGNOSTIC_UNKNOWN};
        std::atomic<bool> cancel{false};
        sc_diagnostic_request request{};
        sc_diagnostic_result result{};
        sc_diagnostic_detail detail{};
        uint64_t admitted_lock_misses = 0;
    };
    // admission/retrieval/claim/housekeeping run under the caller's short lock.
    sc_diagnostic_result submit(const sc_diagnostic_request& request,
                                uint32_t reject, uint64_t now, sc_diagnostic_detail* detail = nullptr);
    sc_diagnostic_result retrieve(const sc_diagnostic_request& request, bool cancel, uint64_t now,
                                  sc_diagnostic_detail* detail = nullptr);
    void note_claim_contention() { claim_lock_misses_.fetch_add(1, std::memory_order_relaxed); }
    Slot* claim(uint64_t now);
    void cancel_pending(uint64_t now);
    void counts(sc_native_snapshot& out, uint64_t now);
    // Only the native callback calls finish. Timeout/cancel of CLAIMED requests
    // requests cancellation, never asserts that the callback did not execute.
    static void finish(Slot& slot, sc_diagnostic_result result, sc_diagnostic_detail detail = {});
private:
    std::atomic<uint64_t> claim_lock_misses_{0};
    void queue_detail(Slot& slot);
    std::array<Slot, SC_DIAGNOSTIC_CAPACITY> slots_{};
    void collect(uint64_t now);
};
}
