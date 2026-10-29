#include "command_registry.h"
#include "native_runtime.h"
#include "commands_native.h"
#include "save_native_hooks.h"
#include <algorithm>
#include <cstring>

namespace sentinel::registry {
namespace {
uint32_t gameplay(const LiveState& live) { return live.process_reason ? live.process_reason : live.gameplay_reason; }
uint32_t save_scope(const char* namespace_id, const LiveState& live) {
    if (const auto why = gameplay(live)) return why;
    if (!live.save_admitted) return SC_NATIVE_BINDING_FAILED;
    return live.namespace_id == namespace_id ? SC_NATIVE_NONE : SC_NATIVE_SCOPE_MISMATCH;
}
uint32_t admit_diagnostic(const Request&, const LiveState& live) { return gameplay(live); }
uint32_t admit_backup(const Request& request, const LiveState& live) {
    return save_scope(std::get<sc_save_backup_request>(request).namespace_id, live);
}
uint32_t admit_points(const Request& request, const LiveState& live) {
    const auto& value = std::get<sc_weapon_points_request>(request);
    if (const auto why = gameplay(live)) return why;
    return weapon_points::admitted(value.namespace_id) ? SC_NATIVE_NONE : SC_NATIVE_SCOPE_MISMATCH;
}
uint32_t admit_inventory(const Request& request, const LiveState& live) {
    if (const auto why = gameplay(live)) return why;
    return inventory::admitted(std::get<sc_inventory_request>(request).namespace_id) ? SC_NATIVE_NONE : SC_NATIVE_SCOPE_MISMATCH;
}
uint32_t admit_arsenal(const Request& request, const LiveState& live) {
    if (const auto why = gameplay(live)) return why;
    return arsenal::admitted(std::get<sc_arsenal_request>(request).namespace_id) ? SC_NATIVE_NONE : SC_NATIVE_SCOPE_MISMATCH;
}
uint32_t admit_runes(const Request& request, const LiveState& live) {
    if (const auto why = gameplay(live)) return why;
    return runes::admitted(std::get<sc_runes_request>(request).namespace_id) ? SC_NATIVE_NONE : SC_NATIVE_SCOPE_MISMATCH;
}
uint32_t admit_special(const Request& request, const LiveState& live) {
    if (const auto why = gameplay(live)) return why;
    if (!special::available()) return SC_NATIVE_BINDING_FAILED;
    return special::admitted(std::get<sc_special_request>(request).namespace_id) ? SC_NATIVE_NONE : SC_NATIVE_SCOPE_MISMATCH;
}
uint32_t admit_deathlink(const Request& request, const LiveState& live) {
    if (const auto why = gameplay(live)) return why;
    return deathlink::admitted(std::get<sc_deathlink_request>(request).namespace_id) ? SC_NATIVE_NONE : SC_NATIVE_SCOPE_MISMATCH;
}
uint32_t admit_command(const Request& request, const LiveState& live) {
    const auto& value = std::get<sc_command_request>(request);
    const auto why = commands::process_scoped(value) ? live.process_reason : gameplay(live);
    if (why) return why;
    return commands::admitted(value) ? SC_NATIVE_NONE : SC_NATIVE_SCOPE_MISMATCH;
}
uint32_t admit_support_dump(const Request& request, const LiveState& live) {
    if (!commands::process_scoped(std::get<sc_command_request>(request))) return SC_NATIVE_SCOPE_MISMATCH;
    return admit_command(request, live);
}
uint32_t admit_process(const Request&, const LiveState& live) { return live.process_reason; }
uint32_t admit_campaign(const Request& request, const LiveState& live) {
    auto save_live = live; save_live.gameplay_reason = 0;
    return save_scope(std::get<generated::NativeCampaign>(request).request.namespace_id, save_live);
}
uint32_t admit_automap(const Request& request, const LiveState& live) {
    auto save_live = live; save_live.gameplay_reason = 0;
    return save_scope(std::get<sc_automap_request>(request).namespace_id, save_live);
}
uint32_t admit_presentation(const Request& request, const LiveState& live) {
    auto save_live = live; save_live.gameplay_reason = 0;
    return save_scope(std::get<sc_combat_presentation_request>(request).namespace_id, save_live);
}
void execute_diagnostic(const Request&, Result&, ExecutionContext&) {}
void execute_backup(const Request&, Result&, ExecutionContext& context) {
    context.submission = save::submit_native_backup(context.backup, context.backup_directory);
}
void execute_points(const Request& request, Result& result, ExecutionContext&) {
    weapon_points::execute_native(std::get<sc_weapon_points_request>(request), std::get<sc_weapon_points_result>(result));
}
void execute_inventory(const Request& request, Result& result, ExecutionContext&) {
    inventory::execute_native(std::get<sc_inventory_request>(request), std::get<sc_inventory_result>(result));
}
void execute_arsenal(const Request& request, Result& result, ExecutionContext&) {
    arsenal::execute_native(std::get<sc_arsenal_request>(request), std::get<sc_arsenal_result>(result));
}
void execute_runes(const Request& request, Result& result, ExecutionContext&) {
    runes::execute_native(std::get<sc_runes_request>(request), std::get<sc_runes_result>(result));
}
void execute_special(const Request& request, Result& result, ExecutionContext&) {
    special::execute_native(std::get<sc_special_request>(request), std::get<sc_special_result>(result));
}
void execute_deathlink(const Request& request, Result& result, ExecutionContext&) {
    deathlink::execute_native(std::get<sc_deathlink_request>(request), std::get<sc_deathlink_result>(result));
}
void execute_command(const Request& request, Result& result, ExecutionContext& context) {
    commands::execute_native(std::get<sc_command_request>(request), std::get<sc_command_result>(result), context.player);
}
template<Id id> void execute_snapshot(const Request& request, Result& result, ExecutionContext& context) {
    result = context.snapshot(id, request);
}
template<uint16_t operation> void execute_campaign(const Request& request, Result& result, ExecutionContext&) {
    const auto& value = std::get<generated::NativeCampaign>(request);
    result = native::campaign_request(operation, value.request, &value.summary, &value.rewards);
}
void execute_automap(const Request& request, Result& result, ExecutionContext&) {
    result = native::automap_request(std::get<sc_automap_request>(request));
}
void execute_presentation(const Request& request, Result& result, ExecutionContext&) {
    result = native::combat_presentation_request(std::get<sc_combat_presentation_request>(request));
}
Evidence entered(const sc_diagnostic_result& execution, EffectEvidence effect) {
    if (!execution.executed_at_ms) return {};
    return {ExecutionEvidence::entered, effect};
}
Evidence observation(const Request&, const Result&, const sc_diagnostic_result& execution) {
    return entered(execution, EffectEvidence::no_effect);
}
Evidence uncertain(const Request&, const Result&, const sc_diagnostic_result& execution) {
    return entered(execution, EffectEvidence::effect_unknown);
}
template<class T> Evidence feature_evidence(const Request& request, const Result&, const sc_diagnostic_result& execution) {
    const auto kind = std::get<T>(request).kind;
    return entered(execution, kind == 0 ? EffectEvidence::no_effect : EffectEvidence::effect_unknown);
}
Evidence special_evidence(const Request& request, const Result&, const sc_diagnostic_result& execution) {
    const auto kind = std::get<sc_special_request>(request).kind;
    return entered(execution, kind == SC_SPECIAL_OBSERVE || kind == SC_SPECIAL_REFILL_STATUS ?
        EffectEvidence::no_effect : EffectEvidence::effect_unknown);
}
Evidence points_evidence(const Request& request, const Result& result, const sc_diagnostic_result& execution) {
    if (!execution.executed_at_ms) return {};
    const auto& r = std::get<sc_weapon_points_request>(request);
    const auto& value = std::get<sc_weapon_points_result>(result);
    if (r.kind == SC_WUP_OBSERVE || !(value.flags & SC_WUP_NATIVE_ENTERED))
        return entered(execution, EffectEvidence::no_effect);
    const auto flags = SC_WUP_BEFORE_VALID | SC_WUP_AFTER_VALID;
    const bool exact = (value.flags & flags) == flags && !value.native_exception &&
        uint64_t(value.balance_before) + r.amount == value.balance_after &&
        uint64_t(value.gained_before) + r.amount == value.gained_after;
    return entered(execution, exact ? EffectEvidence::observed_effect : EffectEvidence::effect_unknown);
}
Evidence command_evidence(const Request&, const Result& result, const sc_diagnostic_result& execution) {
    if (!execution.executed_at_ms) return {};
    const auto& value = std::get<sc_command_result>(result);
    return entered(execution, value.outcome == SC_COMMAND_DISPATCHED && !value.native_exception ?
        EffectEvidence::dispatch_only : EffectEvidence::effect_unknown);
}
Evidence deathlink_evidence(const Request& request, const Result& result, const sc_diagnostic_result& execution) {
    const auto kind = std::get<sc_deathlink_request>(request).kind;
    const auto& value = std::get<sc_deathlink_result>(result);
    if (kind == SC_DEATHLINK_OBSERVE || kind == SC_DEATHLINK_POLL_LOCAL) return entered(execution, EffectEvidence::no_effect);
    if (kind == SC_DEATHLINK_APPLY_REMOTE && (value.outcome == SC_DEATHLINK_OUTCOME_OK || value.outcome == SC_DEATHLINK_OUTCOME_DUPLICATE))
        return entered(execution, EffectEvidence::dispatch_only);
    return entered(execution, value.outcome == SC_DEATHLINK_OUTCOME_OK ? EffectEvidence::accepted_publication : EffectEvidence::effect_unknown);
}
Evidence publication(const Request&, const Result& result, const sc_diagnostic_result&) {
    const bool accepted = std::visit([](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, sc_campaign_result>) return value.status == SC_CAMPAIGN_ACCEPTED;
        else if constexpr (std::is_same_v<T, sc_automap_result>) return value.outcome == SC_AUTOMAP_ACCEPTED;
        else if constexpr (std::is_same_v<T, sc_combat_presentation_result>) return value.status == 0;
        else return false;
    }, result);
    return {ExecutionEvidence::not_entered, accepted ? EffectEvidence::accepted_publication : EffectEvidence::no_effect};
}
template<Id id> Handler make_handler();
template<> Handler make_handler<Id::inspect>() { return {Id::inspect, admit_process, execute_snapshot<Id::inspect>, observation}; }
template<> Handler make_handler<Id::engine>() { return {Id::engine, admit_process, execute_snapshot<Id::engine>, observation}; }
template<> Handler make_handler<Id::context>() { return {Id::context, admit_process, execute_snapshot<Id::context>, observation}; }
template<> Handler make_handler<Id::native>() { return {Id::native, admit_process, execute_snapshot<Id::native>, observation}; }
template<> Handler make_handler<Id::diagnostic>() { return {Id::diagnostic, admit_diagnostic, execute_diagnostic, observation}; }
template<> Handler make_handler<Id::diagnostic_detail>() { return {Id::diagnostic_detail, admit_diagnostic, execute_diagnostic, observation}; }
template<> Handler make_handler<Id::save>() { return {Id::save, admit_process, execute_snapshot<Id::save>, observation}; }
template<> Handler make_handler<Id::save_admission>() { return {Id::save_admission, admit_process, execute_snapshot<Id::save_admission>, observation}; }
template<> Handler make_handler<Id::save_write>() { return {Id::save_write, admit_process, execute_snapshot<Id::save_write>, observation}; }
template<> Handler make_handler<Id::save_backup>() { return {Id::save_backup, admit_backup, execute_backup, uncertain}; }
template<> Handler make_handler<Id::save_installation>() { return {Id::save_installation, admit_process, execute_snapshot<Id::save_installation>, observation}; }
template<> Handler make_handler<Id::weapon_points>() { return {Id::weapon_points, admit_points, execute_points, points_evidence}; }
template<> Handler make_handler<Id::campaign_row>() { return {Id::campaign_row, admit_campaign, execute_campaign<campaign_row_operation>, publication}; }
template<> Handler make_handler<Id::campaign_commit>() { return {Id::campaign_commit, admit_campaign, execute_campaign<campaign_commit_operation>, publication}; }
template<> Handler make_handler<Id::campaign>() { return {Id::campaign, admit_campaign, execute_campaign<campaign_inspect_operation>, observation}; }
template<> Handler make_handler<Id::inventory>() { return {Id::inventory, admit_inventory, execute_inventory, feature_evidence<sc_inventory_request>}; }
template<> Handler make_handler<Id::arsenal>() { return {Id::arsenal, admit_arsenal, execute_arsenal, feature_evidence<sc_arsenal_request>}; }
template<> Handler make_handler<Id::runes>() { return {Id::runes, admit_runes, execute_runes, feature_evidence<sc_runes_request>}; }
template<> Handler make_handler<Id::special>() { return {Id::special, admit_special, execute_special, special_evidence}; }
template<> Handler make_handler<Id::deathlink>() { return {Id::deathlink, admit_deathlink, execute_deathlink, deathlink_evidence}; }
template<> Handler make_handler<Id::automap>() { return {Id::automap, admit_automap, execute_automap, publication}; }
template<> Handler make_handler<Id::command>() { return {Id::command, admit_command, execute_command, command_evidence}; }
template<> Handler make_handler<Id::combat>() { return {Id::combat, admit_process, execute_snapshot<Id::combat>, observation}; }
template<> Handler make_handler<Id::combat_presentation>() { return {Id::combat_presentation, admit_presentation, execute_presentation, publication}; }
template<> Handler make_handler<Id::support_dump>() { return {Id::support_dump, admit_support_dump, execute_command, command_evidence}; }
struct Factory {
    template<Id id> Handler entry() const { return make_handler<id>(); }
};
const auto handlers = generated::registry_handlers(Factory{});
}
const Handler* handler(Id id) {
    const auto found = std::find_if(handlers.begin(), handlers.end(), [id](const Handler& value) { return value.id == id; });
    return found == handlers.end() ? nullptr : &*found;
}
size_t handler_count() { return handlers.size(); }
}
