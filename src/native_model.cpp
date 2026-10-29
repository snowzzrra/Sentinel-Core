#include "native_model.h"
#include "protocol_v2.h"
#include <algorithm>
#include <cstring>
#include <new>
#include <type_traits>
#include <windows.h>
#include <bcrypt.h>

namespace sentinel::native {
static_assert(sizeof(sc_save_backup_request) == 152);
static_assert(sizeof(sc_save_backup_snapshot) == 648);
static_assert(SC_DIAGNOSTIC_CAPACITY == generated::execution_slots);
static_assert(SC_DIAGNOSTIC_RETENTION_MS == generated::full_result_ms);
bool same_scope(const sc_native_scope& a, const sc_native_scope& b) {
    return a.pid == b.pid && a.process_created == b.process_created &&
        a.lifecycle_generation == b.lifecycle_generation &&
        std::memcmp(a.instance_id, b.instance_id, sizeof(a.instance_id)) == 0;
}
void Lifecycle::event(uint32_t kind, uint64_t now, uint32_t tid) {
    ++sequence;
    if (sequence > history.size()) ++overwritten;
    history[(sequence - 1) % history.size()] = {sequence, generation, now, kind, state, tid, depth};
}
void Lifecycle::begin(bool change, bool known, bool flag, uint64_t now, uint32_t tid) {
    if (!depth) {
        ++generation;
        checkpoint_known = change && known; checkpoint = checkpoint_known && flag;
    }
    ++depth; state = SC_LIFETIME_TRANSITION;
    event(change ? SC_EVENT_CHANGE_BEGIN : SC_EVENT_PRIMARY_FREE_BEGIN, now, tid);
}
void Lifecycle::end(bool change, bool success, uint32_t game, uint64_t now, uint32_t tid) {
    if (!depth) { state = SC_LIFETIME_INVALID; event(SC_EVENT_GAP, now, tid); return; }
    --depth;
    if (!depth) state = !change ? SC_LIFETIME_INVALID : (!success ? SC_LIFETIME_FAILED :
        (game == SC_GAME_IN_GAME ? SC_LIFETIME_ACTIVE :
         (game == SC_GAME_MAIN_MENU ? SC_LIFETIME_MENU : SC_LIFETIME_INVALID)));
    event(change ? SC_EVENT_CHANGE_END : SC_EVENT_PRIMARY_FREE_END, now, tid);
}
void Lifecycle::page(sc_native_snapshot& out, uint64_t after) const {
    out.event_sequence = sequence; out.history_overwritten = overwritten;
    out.history_oldest = sequence ? (sequence > history.size() ? sequence - history.size() + 1 : 1) : 0;
    out.history_gap = after && out.history_oldest && after < out.history_oldest - 1;
    const auto first = after ? std::max(after + (after != UINT64_MAX), out.history_oldest) :
        std::max(out.history_oldest, sequence >= SC_NATIVE_EVENT_PAGE ? sequence - SC_NATIVE_EVENT_PAGE + 1 : 1);
    for (uint64_t n = first; n && n <= sequence && out.event_count < SC_NATIVE_EVENT_PAGE; ++n)
        out.events[out.event_count++] = history[(n - 1) % history.size()];
}
namespace {
uint32_t backup_submission_flags(const save::SubmissionResult& submission) {
    return (submission.entered ? SC_BACKUP_NATIVE_ENTERED : 0u) |
        (submission.task_returned ? SC_BACKUP_TASK_RETURNED : 0u) |
        (submission.matched ? SC_BACKUP_SOURCE_MATCHED : 0u);
}
struct CanonicalWriter {
    generated::Bytes& bytes;
    bool valid = true;
    template<class T> void scalar(const T& value, size_t width) {
        auto n = static_cast<uint64_t>(value);
        for (size_t i = 0; i < width; ++i) { bytes.push_back(static_cast<uint8_t>(n)); n >>= 8; }
    }
    template<size_t N> void text(const char (&value)[N]) {
        const auto count = strnlen(value, N);
        if (count == N) { valid = false; return; }
        scalar(count, 2);
        bytes.insert(bytes.end(), value, value + count);
    }
};
template<class T, class = void> struct HasNativeTransfer : std::false_type {};
template<class T> struct HasNativeTransfer<T, std::void_t<decltype(generated::native_transfer(
    std::declval<CanonicalWriter&>(), std::declval<const T&>()))>> : std::true_type {};
sc_diagnostic_request execution_request(const generated::submission& submission) {
    const auto& scope = submission.reference.scope;
    sc_diagnostic_request out{};
    out.expected = {scope.pid, scope.process_created, {}, scope.generation};
    std::copy(scope.instance_id.begin(), scope.instance_id.end(), out.expected.instance_id);
    out.deadline_ms = submission.deadline_ms;
    return out;
}
bool payload_matches(const generated::submission& submission, const generated::NativeRequest& payload) {
    return std::visit([&](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (HasNativeTransfer<T>::value) {
            return
                std::equal(submission.reference.namespace_id.begin(), submission.reference.namespace_id.end(), generated::native_namespace(value));
        } else if constexpr (std::is_same_v<T, sc_diagnostic_request>) {
            return submission.reference.command_type == 5 || submission.reference.command_type == 8;
        }
        else return false;
    }, payload);
}
template<class T, class = void> struct HasExecution : std::false_type {};
template<class T> struct HasExecution<T, std::void_t<decltype(std::declval<T&>().execution = std::declval<sc_diagnostic_result>())>> : std::true_type {};
void result_execution(generated::NativeResult& value, const sc_diagnostic_result& execution) {
    std::visit([&](auto& result) {
        using T = std::decay_t<decltype(result)>;
        if constexpr (HasExecution<T>::value) result.execution = execution;
    }, value);
}
generated::NativeResult initial_value(const generated::NativeRequest& payload) {
    return std::visit([](const auto& r) -> generated::NativeResult {
        using T = std::decay_t<decltype(r)>;
        if constexpr (std::is_same_v<T, sc_weapon_points_request>) return weapon_points::initial(r);
        else if constexpr (std::is_same_v<T, sc_inventory_request>) return inventory::initial(r);
        else if constexpr (std::is_same_v<T, sc_arsenal_request>) return [] (const auto& request) { sc_arsenal_result value{}; arsenal::initial(request, value); return value; }(r);
        else if constexpr (std::is_same_v<T, sc_runes_request>) return runes::initial(r);
        else if constexpr (std::is_same_v<T, sc_special_request>) return special::initial(r);
        else if constexpr (std::is_same_v<T, sc_deathlink_request>) return deathlink::initial(r);
        else if constexpr (std::is_same_v<T, sc_command_request>) return commands::initial(r);
        else return std::monostate{};
    }, payload);
}
template<class... T> bool legacy_payload(generated::NativeRequest& out, const sc_diagnostic_request& r, const T*... inputs) {
    unsigned count = 0;
    auto take = [&](const auto* input) { if (input) { out = *input; ++count; } };
    (take(inputs), ...);
    if (!count) out = r;
    return count <= 1;
}
bool same_payload(const generated::NativeRequest& a, const generated::NativeRequest& b) {
    if (a.index() != b.index()) return false;
    return std::visit([&](const auto& left) {
        using T = std::decay_t<decltype(left)>;
        const auto& right = std::get<T>(b);
        if constexpr (std::is_same_v<T, sc_save_backup_request>) return
            left.campaign == right.campaign && left.slot == right.slot && left.work_deadline_ms == right.work_deadline_ms &&
            std::memcmp(left.namespace_id, right.namespace_id, sizeof(left.namespace_id)) == 0;
        else if constexpr (std::is_same_v<T, sc_weapon_points_request>) return weapon_points::same(left, right);
        else if constexpr (std::is_same_v<T, sc_inventory_request>) return inventory::same(left, right);
        else if constexpr (std::is_same_v<T, sc_arsenal_request>) return arsenal::same(left, right);
        else if constexpr (std::is_same_v<T, sc_runes_request>) return runes::same(left, right);
        else if constexpr (std::is_same_v<T, sc_special_request>) return special::same(left, right);
        else if constexpr (std::is_same_v<T, sc_deathlink_request>) return deathlink::same(left, right);
        else if constexpr (std::is_same_v<T, sc_command_request>) return commands::same(left, right);
        else return std::is_same_v<T, sc_diagnostic_request>;
    }, a);
}
uint32_t rejection(const sc_diagnostic_request& request, bool valid, uint32_t reject) {
    if (reject) return reject;
    if (!valid) return SC_NATIVE_SCOPE_MISMATCH;
    if (!request.deadline_ms || request.deadline_ms > SC_DIAGNOSTIC_MAX_DEADLINE_MS) return SC_NATIVE_DEADLINE;
    return SC_NATIVE_NONE;
}
bool key(const sc_diagnostic_request& a, const sc_diagnostic_request& b) {
    return a.request_id == b.request_id &&
        std::memcmp(a.nonce, b.nonce, sizeof(a.nonce)) == 0;
}
sc_diagnostic_result initial(const sc_diagnostic_request& r) {
    sc_diagnostic_result out{}; out.scope = r.expected; out.request_id = r.request_id;
    std::memcpy(out.nonce, r.nonce, sizeof(out.nonce)); return out;
}
bool same_backup(const Diagnostics::Slot& s, const sc_save_backup_request* b) {
    return s.is<sc_save_backup_request>() == (b != nullptr) && (!b ||
        (s.get<sc_save_backup_request>().campaign == b->campaign && s.get<sc_save_backup_request>().slot == b->slot &&
         s.get<sc_save_backup_request>().work_deadline_ms == b->work_deadline_ms &&
         std::memcmp(s.get<sc_save_backup_request>().namespace_id, b->namespace_id, sizeof(b->namespace_id)) == 0));
}
bool backup_options(const sc_save_backup_request& b) {
    if (b.campaign > 2 || b.slot > 11 || !b.work_deadline_ms ||
        b.work_deadline_ms > SC_SAVE_BACKUP_MAX_WORK_MS || b.namespace_id[64]) return false;
    for (size_t i = 0; i < 64; ++i) {
        const auto c = b.namespace_id[i];
        if (!(c >= '0' && c <= '9') && !(c >= 'a' && c <= 'f')) return false;
    }
    return true;
}
bool valid_payload(const generated::NativeRequest& payload) {
    return std::visit([](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, sc_save_backup_request>) return backup_options(value);
        else if constexpr (std::is_same_v<T, sc_weapon_points_request>) return weapon_points::valid(value);
        else if constexpr (std::is_same_v<T, sc_inventory_request>) return inventory::valid(value);
        else if constexpr (std::is_same_v<T, sc_arsenal_request>) return arsenal::valid(value);
        else if constexpr (std::is_same_v<T, sc_runes_request>) return runes::valid(value);
        else if constexpr (std::is_same_v<T, sc_special_request>) return special::valid(value);
        else if constexpr (std::is_same_v<T, sc_deathlink_request>) return deathlink::valid(value);
        else if constexpr (std::is_same_v<T, sc_command_request>) return commands::valid(value);
        else if constexpr (std::is_same_v<T, generated::NativeCampaign>) return value.request.namespace_id[64] == 0;
        else if constexpr (std::is_same_v<T, sc_automap_request>) return value.namespace_id[64] == 0;
        else if constexpr (std::is_same_v<T, sc_combat_presentation_request>) return value.namespace_id[64] == 0;
        else return std::is_same_v<T, sc_diagnostic_request>;
    }, payload);
}
void bind_execution(Diagnostics::Slot& slot) {
    std::visit([&](auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, generated::NativeCampaign>) value.request.execution = slot.request;
        else if constexpr (HasNativeTransfer<T>::value) value.execution = slot.request;
        else if constexpr (std::is_same_v<T, sc_diagnostic_request>) value = slot.request;
    }, slot.payload);
}
bool same_points(const Diagnostics::Slot& s, const sc_weapon_points_request* p) {
    return s.is<sc_weapon_points_request>() == (p != nullptr) && (!p || weapon_points::same(s.get<sc_weapon_points_request>(), *p));
}
bool same_inventory(const Diagnostics::Slot& s, const sc_inventory_request* inv) {
    return s.is<sc_inventory_request>() == (inv != nullptr) && (!inv || inventory::same(s.get<sc_inventory_request>(), *inv));
}
bool same_arsenal(const Diagnostics::Slot& s, const sc_arsenal_request* ars) {
    return s.is<sc_arsenal_request>() == (ars != nullptr) && (!ars || arsenal::same(s.get<sc_arsenal_request>(), *ars));
}
bool same_runes(const Diagnostics::Slot& s, const sc_runes_request* r) {
    return s.is<sc_runes_request>() == (r != nullptr) && (!r || runes::same(s.get<sc_runes_request>(), *r));
}
bool same_special(const Diagnostics::Slot& s, const sc_special_request* r) {
    return s.is<sc_special_request>() == (r != nullptr) && (!r || special::same(s.get<sc_special_request>(), *r));
}
bool same_deathlink(const Diagnostics::Slot& s, const sc_deathlink_request* r) {
    return s.is<sc_deathlink_request>() == (r != nullptr) && (!r || deathlink::same(s.get<sc_deathlink_request>(), *r));
}
bool same_command(const Diagnostics::Slot& s, const sc_command_request* r) {
    return s.is<sc_command_request>() == (r != nullptr) && (!r || commands::same(s.get<sc_command_request>(), *r));
}
}
void Diagnostics::queue_detail(Slot& s) {
    s.detail.revision = SC_DIAGNOSTIC_DETAIL_REVISION;
    s.detail.stage = SC_STAGE_QUEUE;
    s.detail.claim_lock_missed = claim_lock_misses_.load(std::memory_order_relaxed) != s.admitted_lock_misses;
}
void Diagnostics::collect(uint64_t now) {
    for (auto& s : slots_) {
        auto state = s.state.load(std::memory_order_acquire);
        if (state == SC_DIAGNOSTIC_CLAIMED && s.awaiting_backup.load(std::memory_order_acquire)) {
            complete_backup(s, now);
        } else if (state == SC_DIAGNOSTIC_QUEUED && now >= s.result.deadline_at_ms) {
            queue_detail(s);
            if (s.backup) s.backup->readback_finished(false);
            s.result.state = SC_DIAGNOSTIC_EXPIRED; s.result.reason = SC_NATIVE_DEADLINE;
            s.result.completed_at_ms = now; s.state.store(SC_DIAGNOSTIC_EXPIRED, std::memory_order_release);
        } else if (state >= SC_DIAGNOSTIC_EXECUTED && now >= s.result.completed_at_ms &&
                   now - s.result.completed_at_ms >= SC_DIAGNOSTIC_RETENTION_MS) {
            retire(s);
        }
    }
}
void Diagnostics::complete_backup(Slot& s, uint64_t now) {
    sc_save_backup_snapshot progress{}; s.backup->inspect(progress);
    // a failed copy can precede the outer native read completion. keep the slot while either that read or the real copy still runs
    if ((progress.flags & SC_BACKUP_READ_TERMINAL) && progress.state != SC_BACKUP_COPYING) {
        auto done = s.result; done.completed_at_ms = now;
        done.state = s.submission.entered ? SC_DIAGNOSTIC_EXECUTED : SC_DIAGNOSTIC_REJECTED;
        done.reason = s.submission.exception ? SC_NATIVE_EXCEPTION :
            (s.submission.matched ? SC_NATIVE_NONE : SC_NATIVE_BINDING_FAILED);
        progress.execution = done; progress.native_exception = s.submission.exception;
        progress.flags |= backup_submission_flags(s.submission);
        progress.size = sizeof(progress); progress.abi_version = SC_SAVE_BACKUP_ABI_VERSION;
        const auto& request = s.get<sc_save_backup_request>();
        std::memcpy(progress.namespace_id, request.namespace_id, sizeof(progress.namespace_id));
        progress.campaign = request.campaign; progress.slot = request.slot;
        s.value = progress;
        s.evidence = s.submission.entered ? registry::Evidence{registry::ExecutionEvidence::entered,
            progress.state == SC_BACKUP_COMPLETE ? registry::EffectEvidence::observed_effect : registry::EffectEvidence::effect_unknown} : registry::Evidence{};
        finish(s, done, s.detail);
    }
}
void Diagnostics::archive(Slot& slot) {
    if (!slot.identity) return;
    auto& identity = *slot.identity;
    identity.state = slot.state.load(std::memory_order_acquire);
    identity.reason = slot.result.reason;
    identity.evidence = slot.evidence;
    if (slot.result.executed_at_ms && identity.evidence.execution == registry::ExecutionEvidence::not_entered)
        identity.evidence = {registry::ExecutionEvidence::entered, registry::EffectEvidence::effect_unknown};
}
void Diagnostics::retire(Slot& slot) {
    archive(slot);
    if (slot.identity) slot.identity->slot = nullptr;
    slot.identity = nullptr;
    slot.backup.reset(); slot.payload = {}; slot.value = {};
    slot.state.store(SC_DIAGNOSTIC_UNKNOWN, std::memory_order_release);
}
Diagnostics::Reply Diagnostics::reply(const Identity& identity, uint64_t now) {
    Reply out{}; out.reference = identity.reference; out.result_token = identity.token;
    out.execution.state = identity.state; out.execution.reason = identity.reason;
    out.evidence = identity.evidence; out.retention = Retention::compact_terminal;
    out.execution.scope = {identity.reference.scope.pid, identity.reference.scope.process_created, {}, identity.reference.scope.generation};
    std::copy(identity.reference.scope.instance_id.begin(), identity.reference.scope.instance_id.end(), out.execution.scope.instance_id);
    if (const auto* slot = identity.slot) {
        const auto state = slot->state.load(std::memory_order_acquire);
        out.retention = Retention::full_result;
        out.execution = state == SC_DIAGNOSTIC_CLAIMED ? initial(slot->request) : slot->result;
        out.execution.state = state;
        out.execution.cancel_requested = slot->cancel.load(std::memory_order_acquire);
        if (state == SC_DIAGNOSTIC_CLAIMED) out.evidence = {
            slot->entered.load(std::memory_order_acquire) ? registry::ExecutionEvidence::entered : registry::ExecutionEvidence::unknown,
            registry::EffectEvidence::effect_unknown};
        if (state >= SC_DIAGNOSTIC_EXECUTED) { out.value = slot->value; out.detail = slot->detail; out.evidence = slot->evidence; }
    }
    out.execution.retrieved = 1; out.execution.retrieved_at_ms = now;
    result_execution(out.value, out.execution);
    return out;
}
Diagnostics::Reply Diagnostics::submit(const generated::submission& submission,
        const generated::NativeRequest& payload, uint32_t reject, uint64_t now,
        const registry::Handler* handler, const registry::LiveState* live) {
    collect(now);
    Reply out{};
    const auto& ref = submission.reference;
    std::array<uint8_t, 32> digest{};
    generated::Bytes validation;
    if (!generated::encode(submission, validation) || !v2::fingerprint(submission, payload, digest)) {
        out.error = Error::malformed; return out;
    }
    const IdentityKey key{ref.scope.instance_id, ref.command_id};
    const auto found = identities_.find(key);
    if (found != identities_.end()) {
        if (found->second.reference.fingerprint != digest || !payload_matches(submission, payload)) {
            out.error = Error::duplicate_mismatch; return out;
        }
        return reply(found->second, now);
    }
    const auto* descriptor = registry::descriptor(ref.command_type, ref.payload_revision);
    if (!descriptor || !descriptor->native_matches(payload) || !payload_matches(submission, payload)) {
        out.error = Error::unsupported_revision; return out;
    }
    if (identities_.size() >= generated::identity_records) { out.error = Error::identity_capacity; return out; }
    auto* identity = reserve(submission, digest);
    if (!identity) { out.error = Error::identity_capacity; return out; }
    return admit_identity(*identity, submission, payload, reject, now, handler, live);
}
Diagnostics::Identity* Diagnostics::reserve(const generated::submission& submission, const std::array<uint8_t, 32>& digest) {
    const auto& ref = submission.reference;
    const IdentityKey key{ref.scope.instance_id, ref.command_id};
    Identity* identity = nullptr;
    try {
        Identity value{};
        value.reference = {ref.scope, ref.namespace_id, ref.command_id, ref.command_type, ref.payload_revision, digest};
        identity = &identities_.try_emplace(key, std::move(value)).first->second;
    } catch (const std::bad_alloc&) { return nullptr; }
    return identity;
}
Diagnostics::Reply Diagnostics::admit_identity(Identity& record, const generated::submission& submission,
        const generated::NativeRequest& payload, uint32_t reject, uint64_t now,
        const registry::Handler* handler, const registry::LiveState* live) {
    auto* identity = &record;
    const auto& ref = submission.reference;
    if (BCryptGenRandom(nullptr, identity->token.data(), static_cast<ULONG>(identity->token.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
        reject = SC_NATIVE_BINDING_FAILED;
    if (!reject && !valid_payload(payload)) reject = SC_NATIVE_SCOPE_MISMATCH;
    if (!reject && handler && live) reject = handler->admit(payload, *live);
    identity->state = SC_DIAGNOSTIC_REJECTED; identity->reason = reject;
    if (!reject) {
        identity->slot = allocate(execution_request(submission), payload, now);
        if (identity->slot) {
            identity->slot->identity = identity;
            identity->slot->command_type = static_cast<generated::CommandId>(ref.command_type);
            identity->slot->save_binding = submission.save_binding;
            bind_execution(*identity->slot);
        }
        else identity->reason = SC_NATIVE_QUEUE_FULL;
    }
    return reply(*identity, now);
}
Diagnostics::Reply Diagnostics::query(const generated::command_reference& reference, bool cancel, uint64_t now) {
    collect(now);
    Reply out{}; out.reference = reference;
    const auto found = identities_.find({reference.scope.instance_id, reference.command_id});
    if (found == identities_.end()) { out.error = Error::not_found; return out; }
    const auto& expected = found->second.reference;
    generated::Bytes received, stored;
    if (!generated::encode(reference, received) || !generated::encode(expected, stored) || received != stored) {
        out.error = Error::duplicate_mismatch; return out;
    }
    auto& identity = found->second;
    if (cancel && identity.slot) request_cancel(*identity.slot, now);
    return reply(identity, now);
}
Diagnostics::Reply Diagnostics::ack(const generated::command_reference& reference,
        const std::array<uint8_t, 16>& token, uint64_t now) {
    auto out = query(reference, false, now);
    if (out.error != Error::none) return out;
    if (out.execution.state < SC_DIAGNOSTIC_EXECUTED) { out.error = Error::not_terminal; return out; }
    if (out.result_token != token) { out.error = Error::token_mismatch; return out; }
    auto& identity = identities_.find({reference.scope.instance_id, reference.command_id})->second;
    if (identity.slot) retire(*identity.slot);
    return reply(identity, now);
}
sc_diagnostic_result Diagnostics::submit(const sc_diagnostic_request& r, uint32_t reject, uint64_t now,
        sc_diagnostic_detail* detail, const sc_save_backup_request* backup, const sc_weapon_points_request* points,
        const sc_inventory_request* inventory, const sc_arsenal_request* arsenal,
        const sc_runes_request* runes, const sc_special_request* special,
        const sc_deathlink_request* deathlink, const sc_command_request* command) {
    if (detail) { *detail = {}; detail->revision = SC_DIAGNOSTIC_DETAIL_REVISION; detail->stage = SC_STAGE_ADMISSION; }
    generated::NativeRequest payload{};
    const bool shape = legacy_payload(payload, r, backup, points, inventory, arsenal, runes, special, deathlink, command);
    collect(now);
    auto out = initial(r);
    for (auto& s : slots_) if (s.state.load(std::memory_order_acquire) != SC_DIAGNOSTIC_UNKNOWN && !s.identity && key(s.request, r)) {
        if (!shape || !same_scope(s.request.expected, r.expected) || s.request.deadline_ms != r.deadline_ms ||
            !same_payload(s.payload, payload)) {
            out.state = SC_DIAGNOSTIC_REJECTED; out.reason = SC_NATIVE_DUPLICATE_MISMATCH; return out;
        }
        return retrieve(r, false, now, detail, backup, points, inventory, arsenal, runes, special, deathlink, command);
    }
    reject = rejection(r, shape && valid_payload(payload), reject);
    if (reject) { out.state = SC_DIAGNOSTIC_REJECTED; out.reason = reject; return out; }
    auto* slot = allocate(r, payload, now);
    if (!slot) { out.state = SC_DIAGNOSTIC_REJECTED; out.reason = SC_NATIVE_QUEUE_FULL; return out; }
    if (detail) *detail = slot->detail;
    return slot->result;
}
Diagnostics::Slot* Diagnostics::allocate(const sc_diagnostic_request& r, const generated::NativeRequest& payload, uint64_t now) {
    auto out = initial(r);
    const auto* backup = std::get_if<sc_save_backup_request>(&payload);
    for (auto& s : slots_) if (s.state.load(std::memory_order_acquire) == SC_DIAGNOSTIC_UNKNOWN) {
        std::shared_ptr<save::BackupJob> job;
        if (backup) try {
            job = std::make_shared<save::BackupJob>(r.expected.pid, r.expected.process_created, now + backup->work_deadline_ms);
        } catch (const std::bad_alloc&) {
            return nullptr;
        }
        s.payload = payload;
        s.value = initial_value(payload);
        s.entered.store(false, std::memory_order_relaxed);
        s.identity = nullptr; s.transient_read = false; s.evidence = {};
        s.command_type = std::visit([](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, std::monostate>) return generated::CommandId::diagnostic;
            else return generated::NativeBinding<T>::id;
        }, payload);
        s.save_binding = {};
        s.backup = std::move(job); s.submission = {};
        s.awaiting_backup.store(false, std::memory_order_relaxed);
        s.request = r; s.cancel.store(false, std::memory_order_relaxed);
        s.detail = {}; s.detail.revision = SC_DIAGNOSTIC_DETAIL_REVISION;
        s.admitted_lock_misses = claim_lock_misses_.load(std::memory_order_relaxed);
        s.detail.stage = SC_STAGE_QUEUE;
        out.state = SC_DIAGNOSTIC_QUEUED; out.admitted_at_ms = now; out.deadline_at_ms = now + r.deadline_ms;
        s.result = out; s.state.store(SC_DIAGNOSTIC_QUEUED, std::memory_order_release); return &s;
    }
    return nullptr;
}

bool Diagnostics::matches_legacy(const Slot& slot, const sc_diagnostic_request& r, const generated::NativeRequest& payload) {
    return slot.state.load(std::memory_order_acquire) != SC_DIAGNOSTIC_UNKNOWN && !slot.identity &&
        key(slot.request, r) && same_scope(slot.request.expected, r.expected) && same_payload(slot.payload, payload) &&
        (std::holds_alternative<sc_diagnostic_request>(payload) || slot.request.deadline_ms == r.deadline_ms);
}
void Diagnostics::request_cancel(Slot& slot, uint64_t now) {
    const auto state = slot.state.load(std::memory_order_acquire);
    if (state != SC_DIAGNOSTIC_QUEUED && state != SC_DIAGNOSTIC_CLAIMED) return;
    slot.cancel.store(true, std::memory_order_release);
    if (slot.backup) slot.backup->cancel();
    if (state == SC_DIAGNOSTIC_QUEUED) {
        queue_detail(slot);
        auto result = slot.result; result.state = SC_DIAGNOSTIC_CANCELLED;
        result.reason = SC_NATIVE_CANCELLED; result.completed_at_ms = now;
        finish(slot, result, slot.detail);
    }
}
sc_diagnostic_result Diagnostics::retrieve(const sc_diagnostic_request& r, bool cancel, uint64_t now,
        sc_diagnostic_detail* detail, const sc_save_backup_request* backup, const sc_weapon_points_request* points,
        const sc_inventory_request* inventory, const sc_arsenal_request* arsenal,
        const sc_runes_request* runes, const sc_special_request* special,
        const sc_deathlink_request* deathlink, const sc_command_request* command) {
    if (detail) { *detail = {}; detail->revision = SC_DIAGNOSTIC_DETAIL_REVISION; }
    collect(now);
    generated::NativeRequest payload{};
    const bool shape = legacy_payload(payload, r, backup, points, inventory, arsenal, runes, special, deathlink, command);
    for (auto& s : slots_) {
        if (!shape || !matches_legacy(s, r, payload)) continue;
        if (cancel) request_cancel(s, now);
        const auto state = s.state.load(std::memory_order_acquire);
        // the callback owns mutable facts until terminal publication
        auto out = state == SC_DIAGNOSTIC_CLAIMED ? initial(s.request) : s.result;
        if (detail && state != SC_DIAGNOSTIC_CLAIMED) *detail = s.detail;
        out.state = state; out.cancel_requested = s.cancel.load(std::memory_order_acquire);
        out.retrieved = 1; out.retrieved_at_ms = now; return out;
    }
    auto out = initial(r); out.reason = SC_NATIVE_NOT_FOUND; out.retrieved = 1; out.retrieved_at_ms = now; return out;
}
sc_weapon_points_result Diagnostics::points_result(const sc_weapon_points_request& r, bool cancel, uint64_t now, bool release) {
    auto out = weapon_points::initial(r);
    const auto execution = retrieve(r.execution, cancel, now, nullptr, nullptr, &r, nullptr, nullptr, nullptr);
    if (execution.state >= SC_DIAGNOSTIC_EXECUTED) {
        for (auto& s : slots_) {
            // native facts belong exclusively to the callback until terminal release
            if (s.state.load(std::memory_order_acquire) >= SC_DIAGNOSTIC_EXECUTED &&
                same_points(s,&r) && key(s.request,r.execution) && same_scope(s.request.expected,r.execution.expected)) {
                out = s.output<sc_weapon_points_result>();
                if (release) retire(s);
                break;
            }
        }
    }
    out.execution = execution; return out;
}
sc_inventory_result Diagnostics::inventory_result(const sc_inventory_request& r, bool cancel, uint64_t now, bool release) {
    sc_inventory_result out = inventory::initial(r);
    const auto execution = retrieve(r.execution, cancel, now, nullptr, nullptr, nullptr, &r, nullptr, nullptr);
    if (execution.state >= SC_DIAGNOSTIC_EXECUTED) {
        for (auto& s : slots_) {
            if (s.state.load(std::memory_order_acquire) >= SC_DIAGNOSTIC_EXECUTED &&
                same_inventory(s, &r) && key(s.request, r.execution) && same_scope(s.request.expected, r.execution.expected)) {
                out = s.output<sc_inventory_result>();
                if (release) retire(s);
                break;
            }
        }
    }
    out.execution = execution; return out;
}
sc_arsenal_result Diagnostics::arsenal_result(const sc_arsenal_request& r, bool cancel, uint64_t now, bool release) {
    sc_arsenal_result out{}; arsenal::initial(r, out);
    const auto execution = retrieve(r.execution, cancel, now, nullptr, nullptr, nullptr, nullptr, &r, nullptr);
    if (execution.state >= SC_DIAGNOSTIC_EXECUTED) {
        for (auto& s : slots_) {
            if (s.state.load(std::memory_order_acquire) >= SC_DIAGNOSTIC_EXECUTED &&
                same_arsenal(s, &r) && key(s.request, r.execution) && same_scope(s.request.expected, r.execution.expected)) {
                out = s.output<sc_arsenal_result>();
                if (release) retire(s);
                break;
            }
        }
    }
    out.execution = execution; return out;
}
sc_runes_result Diagnostics::runes_result(const sc_runes_request& r, bool cancel, uint64_t now, bool release) {
    sc_runes_result out = runes::initial(r);
    const auto execution = retrieve(r.execution, cancel, now, nullptr, nullptr, nullptr, nullptr, nullptr, &r);
    if (execution.state >= SC_DIAGNOSTIC_EXECUTED) {
        for (auto& s : slots_) {
            if (s.state.load(std::memory_order_acquire) >= SC_DIAGNOSTIC_EXECUTED &&
                same_runes(s, &r) && key(s.request, r.execution) && same_scope(s.request.expected, r.execution.expected)) {
                out = s.output<sc_runes_result>();
                if (release) retire(s);
                break;
            }
        }
    }
    out.execution = execution; return out;
}
sc_special_result Diagnostics::special_result(const sc_special_request& r, bool cancel, uint64_t now, bool release) {
    sc_special_result out = special::initial(r);
    const auto execution = retrieve(r.execution, cancel, now, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &r);
    if (execution.state >= SC_DIAGNOSTIC_EXECUTED) {
        for (auto& s : slots_) {
            if (s.state.load(std::memory_order_acquire) >= SC_DIAGNOSTIC_EXECUTED &&
                same_special(s, &r) && key(s.request, r.execution) && same_scope(s.request.expected, r.execution.expected)) {
                out = s.output<sc_special_result>();
                if (release) retire(s);
                break;
            }
        }
    }
    out.execution = execution; return out;
}
sc_deathlink_result Diagnostics::deathlink_result(const sc_deathlink_request& r, bool cancel, uint64_t now, bool release) {
    sc_deathlink_result out = deathlink::initial(r);
    const auto execution = retrieve(r.execution, cancel, now, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &r);
    if (execution.state >= SC_DIAGNOSTIC_EXECUTED) {
        for (auto& s : slots_) {
            if (s.state.load(std::memory_order_acquire) >= SC_DIAGNOSTIC_EXECUTED &&
                same_deathlink(s, &r) && key(s.request, r.execution) && same_scope(s.request.expected, r.execution.expected)) {
                out = s.output<sc_deathlink_result>();
                if (release) retire(s);
                break;
            }
        }
    }
    out.execution = execution; return out;
}
sc_command_result Diagnostics::command_result(const sc_command_request& r, bool cancel, uint64_t now, bool release) {
    auto out = commands::initial(r);
    const auto execution = retrieve(r.execution, cancel, now, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &r);
    if (execution.state >= SC_DIAGNOSTIC_EXECUTED) {
        for (auto& s : slots_) {
            if (s.state.load(std::memory_order_acquire) >= SC_DIAGNOSTIC_EXECUTED &&
                same_command(s, &r) && key(s.request, r.execution) && same_scope(s.request.expected, r.execution.expected)) {
                out = s.output<sc_command_result>();
                if (release) retire(s);
                break;
            }
        }
    }
    out.execution = execution; return out;
}
Diagnostics::Slot* Diagnostics::claim(uint64_t now) {
    collect(now);
    for (auto& s : slots_) if (s.state.load(std::memory_order_acquire) == SC_DIAGNOSTIC_QUEUED) {
        queue_detail(s);
        s.result.state = SC_DIAGNOSTIC_CLAIMED; s.result.claimed_at_ms = now;
        s.state.store(SC_DIAGNOSTIC_CLAIMED, std::memory_order_release); return &s;
    }
    return nullptr;
}
Diagnostics::Slot* Diagnostics::claim(const generated::command_reference& reference, uint64_t now) {
    const auto existing = query(reference, false, now);
    if (existing.error != Error::none || existing.execution.state != SC_DIAGNOSTIC_QUEUED) return nullptr;
    auto* slot = identities_.find({reference.scope.instance_id, reference.command_id})->second.slot;
    queue_detail(*slot);
    slot->result.state = SC_DIAGNOSTIC_CLAIMED; slot->result.claimed_at_ms = now;
    slot->state.store(SC_DIAGNOSTIC_CLAIMED, std::memory_order_release);
    return slot;
}
void Diagnostics::finish(Slot& s, sc_diagnostic_result result, sc_diagnostic_detail detail) {
    if (s.backup && !s.awaiting_backup.load(std::memory_order_acquire)) s.backup->readback_finished(false);
    result.cancel_requested = s.cancel.load(std::memory_order_acquire);
    detail.revision = SC_DIAGNOSTIC_DETAIL_REVISION;
    s.detail = detail; s.result = result; s.state.store(result.state, std::memory_order_release);
}
void Diagnostics::await_backup(Slot& s, sc_diagnostic_result result, sc_diagnostic_detail detail,
        save::SubmissionResult submission) {
    result.state = SC_DIAGNOSTIC_CLAIMED; result.completed_at_ms = 0;
    s.submission = submission; s.result = result; s.detail = detail;
    s.awaiting_backup.store(true, std::memory_order_release);
    // only the job may be accessed after publishing this handoff
}
sc_save_backup_snapshot Diagnostics::backup_result(const sc_save_backup_request& r, bool cancel, uint64_t now) {
    sc_save_backup_snapshot out{}; out.size = sizeof(out); out.abi_version = SC_SAVE_BACKUP_ABI_VERSION;
    out.execution = retrieve(r.execution, cancel, now, nullptr, &r);
    if (out.execution.cancel_requested) out.flags = SC_BACKUP_CANCEL_REQUESTED;
    std::memcpy(out.namespace_id, r.namespace_id, sizeof(out.namespace_id)); out.campaign = r.campaign; out.slot = r.slot;
    switch (out.execution.state) {
    case SC_DIAGNOSTIC_UNKNOWN: return out;
    case SC_DIAGNOSTIC_QUEUED: out.state = SC_BACKUP_QUEUED; break;
    case SC_DIAGNOSTIC_CLAIMED: out.state = SC_BACKUP_CLAIMED; break;
    case SC_DIAGNOSTIC_REJECTED: out.state = SC_BACKUP_REJECTED; break;
    case SC_DIAGNOSTIC_EXPIRED: out.state = SC_BACKUP_EXPIRED; break;
    case SC_DIAGNOSTIC_CANCELLED: out.state = SC_BACKUP_CANCELLED; break;
    default: out.state = SC_BACKUP_FAILED; break;
    }
    for (const auto& s : slots_) {
        if (s.state.load(std::memory_order_acquire) == SC_DIAGNOSTIC_UNKNOWN || !same_backup(s, &r) ||
            !key(s.request, r.execution) || !same_scope(s.request.expected, r.execution.expected)) continue;
        // callback-owned submission/result fields are invisible until handoff
        if (!s.awaiting_backup.load(std::memory_order_acquire)) return out;
        out.execution = s.result; out.execution.cancel_requested = s.cancel.load(std::memory_order_acquire);
        out.execution.retrieved = 1; out.execution.retrieved_at_ms = now;
        out.native_exception = s.submission.exception;
        out.flags = backup_submission_flags(s.submission);
        s.backup->inspect(out);
        if (out.execution.state == SC_DIAGNOSTIC_CLAIMED) {
            if (out.state != SC_BACKUP_COPYING && out.state != SC_BACKUP_COPIED) out.state = SC_BACKUP_WAITING_NATIVE;
        } else if (out.execution.state == SC_DIAGNOSTIC_EXECUTED) {
            out.state = s.submission.matched && out.state == SC_BACKUP_COMPLETE ? SC_BACKUP_COMPLETE : SC_BACKUP_FAILED;
            if (!s.submission.matched) out.failure = SC_BACKUP_FAILURE_NATIVE;
        } else if (out.execution.state == SC_DIAGNOSTIC_REJECTED) out.state = SC_BACKUP_REJECTED;
        return out;
    }
    return out;
}
void Diagnostics::cancel_pending(uint64_t now) {
    for (auto& s : slots_) {
        const auto state = s.state.load(std::memory_order_acquire);
        if (s.backup && (state == SC_DIAGNOSTIC_CLAIMED || state == SC_DIAGNOSTIC_QUEUED)) s.backup->cancel();
        if (state == SC_DIAGNOSTIC_CLAIMED) s.cancel.store(true, std::memory_order_release);
        if (state == SC_DIAGNOSTIC_QUEUED) {
            if (s.backup) s.backup->readback_finished(false);
            queue_detail(s);
            s.result.state = SC_DIAGNOSTIC_CANCELLED; s.result.reason = SC_NATIVE_STOPPED;
            s.result.completed_at_ms = now; s.state.store(SC_DIAGNOSTIC_CANCELLED, std::memory_order_release);
        }
    }
}
void Diagnostics::counts(sc_native_snapshot& out, uint64_t now) {
    collect(now);
    for (auto& s : slots_) {
        const auto state = s.state.load(std::memory_order_acquire);
        out.queued += state == SC_DIAGNOSTIC_QUEUED;
        out.claimed += state == SC_DIAGNOSTIC_CLAIMED;
        out.retained_results += state >= SC_DIAGNOSTIC_EXECUTED;
    }
}
}
namespace sentinel::native {
Diagnostics::Slot* Diagnostics::transient(const sc_diagnostic_request& request, const generated::NativeRequest& payload,
        uint32_t reject, uint64_t now) {
    collect(now);
    if (reject || !valid_payload(payload)) return nullptr;
    return allocate(request, payload, now);
}
bool Diagnostics::collect_transient(Slot& slot, Reply& out, uint64_t now) {
    const auto state = slot.state.load(std::memory_order_acquire);
    if (state == SC_DIAGNOSTIC_QUEUED || state == SC_DIAGNOSTIC_CLAIMED) {
        if (state == SC_DIAGNOSTIC_QUEUED && now >= slot.result.deadline_at_ms) request_cancel(slot, now);
        return false;
    }
    out.execution = slot.result; out.value = slot.value; out.evidence = slot.evidence;
    out.retention = Retention::full_result; result_execution(out.value, out.execution); retire(slot); return true;
}
}
