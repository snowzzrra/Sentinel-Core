#pragma once
#include "sentinel_combat.h"
#include <cstring>
#include <mutex>

namespace sentinel::combat {
inline bool valid_presentation(const sc_combat_presentation_request& request) {
    if (request.namespace_id[64] || request.visible > 1 || request.milestone > request.total ||
        (request.target && request.target <= request.total) || (request.visible && !request.target)) return false;
    if (request.milestone ? (!request.first_milestone || request.first_milestone > request.milestone ||
        !request.milestone_step || (request.milestone - request.first_milestone) % request.milestone_step) :
        (request.first_milestone || request.milestone_step)) return false;
    for (unsigned i = 0; i < 64; ++i) {
        const auto c = request.namespace_id[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}
struct ProgressView { uint64_t total = 0, target = 0, milestone = 0; bool visible = false; };
class Presentation {
    std::mutex mutex_;
    char namespace_id_[65]{};
    ProgressView current_{};
    uint64_t last_notice_ = 0;
    uint64_t notice_first_ = 0, notice_last_ = 0, notice_step_ = 0;
public:
    sc_combat_presentation_result submit(const sc_combat_presentation_request& request,
                                       const sc_native_scope& scope, bool admitted) {
        sc_combat_presentation_result result{};
        result.size = sizeof(result); result.abi_version = SC_COMBAT_PRESENTATION_ABI_VERSION;
        result.scope = scope; result.request_id = request.execution.request_id;
        std::memcpy(result.nonce, request.execution.nonce, sizeof(result.nonce));
        result.status = 1; result.reason = SC_NATIVE_SCOPE_MISMATCH;
        if (!admitted) return result;
        if (!valid_presentation(request)) { result.reason = SC_NATIVE_READ_FAILED; return result; }
        std::lock_guard lock(mutex_);
        const bool same = !std::memcmp(namespace_id_, request.namespace_id, sizeof(namespace_id_));
        if (same && request.total < current_.total) { result.reason = SC_NATIVE_STALE; return result; }
        if (!same) {
            std::memcpy(namespace_id_, request.namespace_id, sizeof(namespace_id_));
            notice_first_ = notice_last_ = notice_step_ = last_notice_ = 0;
        }
        if (request.milestone > last_notice_) {
            auto first = request.first_milestone;
            if (first <= last_notice_)
                first += ((last_notice_ - first) / request.milestone_step + 1) * request.milestone_step;
            if (notice_first_ && (notice_step_ != request.milestone_step ||
                first - notice_last_ != notice_step_)) { result.reason = SC_NATIVE_STALE; return result; }
            if (!notice_first_) notice_first_ = first;
            notice_last_ = last_notice_ = request.milestone; notice_step_ = request.milestone_step;
        }
        current_ = {request.total, request.target, 0, request.visible != 0};
        result.status = 0; result.reason = SC_NATIVE_NONE;
        result.total = request.total; result.target = request.target;
        result.milestone = request.milestone; result.visible = request.visible;
        return result;
    }
    ProgressView view(const char* namespace_id) {
        std::lock_guard lock(mutex_);
        if (!namespace_id || std::memcmp(namespace_id_, namespace_id, sizeof(namespace_id_))) return {};
        auto value = current_;
        value.milestone = notice_first_;
        return value;
    }
    void notice_presented(const char* namespace_id, uint64_t milestone) {
        std::lock_guard lock(mutex_);
        if (!std::memcmp(namespace_id_, namespace_id, sizeof(namespace_id_)) &&
            notice_first_ == milestone) {
            if (notice_first_ == notice_last_) notice_first_ = notice_last_ = notice_step_ = 0;
            else notice_first_ += notice_step_;
        }
    }
};
inline Presentation& presentation() { static Presentation value; return value; }
}
