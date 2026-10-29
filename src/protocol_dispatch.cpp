#include "protocol_dispatch.h"
#include "protocol_v2.h"
#include "inspection_server.h"
#include "native_runtime.h"
#include "combat_native.h"
#include "save_session.h"

namespace sentinel {
namespace {
registry::Result registry_snapshot(registry::Id id, const registry::Request& request) {
    switch (id) {
    case registry::Id::inspect: return current_snapshot().core;
    case registry::Id::engine: return current_engine_snapshot();
    case registry::Id::context: return current_context_snapshot();
    case registry::Id::native: return native::inspect(std::get<generated::NativeRead>(request).cursor);
    case registry::Id::save: return current_save_snapshot();
    case registry::Id::save_admission: return save::session().inspect();
    case registry::Id::save_write: return save::session().native_writes.snapshot(std::get<generated::NativeRead>(request).cursor);
    case registry::Id::save_installation: return save::session().installation.inspect();
    case registry::Id::combat: return combat::inspect(std::get<sc_combat_query>(request));
    default: return std::monostate{};
    }
}
template<class T> T registry_result(registry::Id id, const registry::Request& request = generated::NativeRead{}) {
    registry::Result result{}; registry::ExecutionContext context{}; context.snapshot = registry_snapshot;
    registry::handler(id)->execute(request, result, context);
    return std::get<T>(result);
}
size_t inspect_message(Message& data, const DecodedRequest&) {
    return encode_response(data, WireResult::ok, current_snapshot());
}
size_t engine_message(Message& data, const DecodedRequest&) {
    return encode_engine_response(data, WireResult::ok, current_snapshot(), registry_result<sc_engine_snapshot>(registry::Id::engine));
}
size_t context_message(Message& data, const DecodedRequest&) {
    return encode_context_response(data, WireResult::ok, current_snapshot(), registry_result<sc_context_snapshot>(registry::Id::context));
}
size_t native_message(Message& data, const DecodedRequest& request) {
    return encode_native_response(data, WireResult::ok, request.operation, current_snapshot(),
                                  registry_result<sc_native_snapshot>(registry::Id::native, generated::NativeRead{request.cursor}), {});
}
template<uint16_t Operation> size_t diagnostic_message(Message& data, const DecodedRequest& request) {
    sc_diagnostic_detail detail{};
    const auto result = Operation == diagnostic_submit_operation || Operation == diagnostic_detail_submit_operation ?
        native::submit(request.execution, &detail) : native::result(request.execution,
            Operation == diagnostic_cancel_operation || Operation == diagnostic_detail_cancel_operation, &detail);
    return encode_native_response(data, WireResult::ok, Operation, current_snapshot(), native::inspect(), result, detail);
}
size_t save_message(Message& data, const DecodedRequest&) {
    return encode_save_response(data, WireResult::ok, current_snapshot(), registry_result<sc_save_snapshot>(registry::Id::save));
}
size_t admission_message(Message& data, const DecodedRequest&) {
    return encode_save_admission_response(data, WireResult::ok, current_snapshot(), registry_result<sc_save_admission_snapshot>(registry::Id::save_admission));
}
size_t write_message(Message& data, const DecodedRequest& request) {
    return encode_save_write_response(data, WireResult::ok, current_snapshot(),
                                      registry_result<sc_save_write_snapshot>(registry::Id::save_write, generated::NativeRead{request.cursor}));
}
size_t installation_message(Message& data, const DecodedRequest&) {
    return encode_installation_response(data, WireResult::ok, current_snapshot(), registry_result<sc_save_installation_snapshot>(registry::Id::save_installation));
}
template<uint16_t Operation> size_t backup_message(Message& data, const DecodedRequest& request) {
    const auto& value = std::get<sc_save_backup_request>(request.payload);
    const auto result = Operation == save_backup_submit_operation ? native::submit_backup(value) :
        native::backup_result(value, Operation == save_backup_cancel_operation);
    return encode_backup_response(data, WireResult::ok, Operation, current_snapshot(), result);
}
template<uint16_t Operation> size_t weapon_points_message(Message& data, const DecodedRequest& request) {
    const auto& value = std::get<sc_weapon_points_request>(request.payload);
    const auto result = Operation == weapon_points_submit_operation ? native::submit_weapon_points(value) :
        native::weapon_points_result(value, Operation == weapon_points_cancel_operation, Operation == weapon_points_release_operation);
    return encode_weapon_points_response(data, WireResult::ok, Operation, current_snapshot(), result);
}
template<uint16_t Operation> size_t inventory_message(Message& data, const DecodedRequest& request) {
    const auto& value = std::get<sc_inventory_request>(request.payload);
    const auto result = Operation == inventory_submit_operation ? native::submit_inventory(value) :
        native::inventory_result(value, Operation == inventory_cancel_operation, Operation == inventory_release_operation);
    return encode_inventory_response(data, WireResult::ok, Operation, current_snapshot(), result);
}
template<uint16_t Operation> size_t arsenal_message(Message& data, const DecodedRequest& request) {
    const auto& value = std::get<sc_arsenal_request>(request.payload);
    const auto result = Operation == arsenal_submit_operation ? native::submit_arsenal(value) :
        native::arsenal_result(value, Operation == arsenal_cancel_operation, Operation == arsenal_release_operation);
    return encode_arsenal_response(data, WireResult::ok, Operation, current_snapshot(), result);
}
template<uint16_t Operation> size_t runes_message(Message& data, const DecodedRequest& request) {
    const auto& value = std::get<sc_runes_request>(request.payload);
    const auto result = Operation == runes_submit_operation ? native::submit_runes(value) :
        native::runes_result(value, Operation == runes_cancel_operation, Operation == runes_release_operation);
    return encode_runes_response(data, WireResult::ok, Operation, current_snapshot(), result);
}
template<uint16_t Operation> size_t special_message(Message& data, const DecodedRequest& request) {
    const auto& value = std::get<sc_special_request>(request.payload);
    const auto result = Operation == special_submit_operation ? native::submit_special(value) :
        native::special_result(value, Operation == special_cancel_operation, Operation == special_release_operation);
    return encode_special_response(data, WireResult::ok, Operation, current_snapshot(), result);
}
template<uint16_t Operation> size_t deathlink_message(Message& data, const DecodedRequest& request) {
    const auto& value = std::get<sc_deathlink_request>(request.payload);
    const auto result = Operation == deathlink_submit_operation ? native::submit_deathlink(value) :
        native::deathlink_result(value, Operation == deathlink_cancel_operation, Operation == deathlink_release_operation);
    return encode_deathlink_response(data, WireResult::ok, Operation, current_snapshot(), result);
}
template<uint16_t Operation> size_t command_message(Message& data, const DecodedRequest& request) {
    const auto& value = std::get<sc_command_request>(request.payload);
    const auto result = Operation == command_submit_operation ? native::submit_command(value) :
        native::command_result(value, Operation == command_cancel_operation, Operation == command_release_operation);
    return encode_command_response(data, WireResult::ok, Operation, current_snapshot(), result);
}
template<uint16_t Operation> size_t campaign_message(Message& data, const DecodedRequest& request) {
    const auto& value = std::get<CampaignRequest>(request.payload);
    constexpr auto id = Operation == campaign_row_operation ? registry::Id::campaign_row :
        (Operation == campaign_commit_operation ? registry::Id::campaign_commit : registry::Id::campaign);
    const auto result = registry_result<sc_campaign_result>(id, generated::NativeCampaign{value.request, value.summary, value.rewards});
    return encode_campaign_response(data, WireResult::ok, Operation, current_snapshot(), result);
}
size_t automap_message(Message& data, const DecodedRequest& request) {
    const auto result = registry_result<sc_automap_result>(registry::Id::automap, std::get<sc_automap_request>(request.payload));
    return encode_automap_response(data, WireResult::ok, current_snapshot(), result);
}
size_t combat_message(Message& data, const DecodedRequest& request) {
    return encode_combat_response(data, WireResult::ok, current_snapshot(),
                                  registry_result<sc_combat_snapshot>(registry::Id::combat, std::get<sc_combat_query>(request.payload)));
}
size_t presentation_message(Message& data, const DecodedRequest& request) {
    const auto result = registry_result<sc_combat_presentation_result>(registry::Id::combat_presentation, std::get<sc_combat_presentation_request>(request.payload));
    return encode_combat_presentation_response(data, WireResult::ok, current_snapshot(), result);
}
constexpr MessageHandlers handlers = {
    inspect_message, engine_message, context_message, native_message,
    diagnostic_message<diagnostic_submit_operation>, diagnostic_message<diagnostic_result_operation>,
    diagnostic_message<diagnostic_cancel_operation>, diagnostic_message<diagnostic_detail_submit_operation>,
    diagnostic_message<diagnostic_detail_result_operation>, diagnostic_message<diagnostic_detail_cancel_operation>,
    save_message, admission_message, write_message,
    backup_message<save_backup_submit_operation>, backup_message<save_backup_result_operation>,
    backup_message<save_backup_cancel_operation>, installation_message,
    weapon_points_message<weapon_points_submit_operation>,
    weapon_points_message<weapon_points_result_operation>,
    weapon_points_message<weapon_points_cancel_operation>,
    weapon_points_message<weapon_points_release_operation>,
    campaign_message<campaign_row_operation>,
    campaign_message<campaign_commit_operation>,
    campaign_message<campaign_inspect_operation>,
    inventory_message<inventory_submit_operation>,
    inventory_message<inventory_result_operation>,
    inventory_message<inventory_cancel_operation>,
    inventory_message<inventory_release_operation>,
    arsenal_message<arsenal_submit_operation>,
    arsenal_message<arsenal_result_operation>,
    arsenal_message<arsenal_cancel_operation>,
    arsenal_message<arsenal_release_operation>,
    runes_message<runes_submit_operation>,
    runes_message<runes_result_operation>,
    runes_message<runes_cancel_operation>,
    runes_message<runes_release_operation>,
    special_message<special_submit_operation>,
    special_message<special_result_operation>,
    special_message<special_cancel_operation>,
    special_message<special_release_operation>,
    deathlink_message<deathlink_submit_operation>,
    deathlink_message<deathlink_result_operation>,
    deathlink_message<deathlink_cancel_operation>,
    deathlink_message<deathlink_release_operation>,
    automap_message,
    command_message<command_submit_operation>,
    command_message<command_result_operation>,
    command_message<command_cancel_operation>,
    command_message<command_release_operation>,
    combat_message,
    presentation_message,
};
constexpr bool handlers_complete() {
    for (size_t i = 0; i < handlers.size(); ++i)
        if (!handlers[i] || generated::v1_descriptors[i].version != wire_version ||
            generated::v1_descriptors[i].opcode != i + 1) return false;
    return true;
}
static_assert(handlers_complete());
}
size_t process_message(Message& data, size_t size) {
    return process_message(data, size, handlers);
}
}
namespace sentinel {
namespace {
generated::scope v2_scope() {
    const auto snapshot = current_snapshot();
    return {snapshot.pid, snapshot.process_created, snapshot.instance, native::inspect().scope.lifecycle_generation};
}
bool v2_identity(const generated::scope& expected) {
    const auto actual = current_snapshot();
    return expected.pid == actual.pid && expected.process_created == actual.process_created && expected.instance_id == actual.instance;
}
void append(generated::Bytes& out, const generated::Bytes& bytes) { out.insert(out.end(), bytes.begin(), bytes.end()); }
generated::command_reference reference(const generated::submission_identity& input) {
    return {input.scope, input.namespace_id, input.command_id, input.command_type, input.payload_revision, {}};
}
generated::Bytes command_body(const native::Diagnostics::Reply& result, const generated::command_reference& echoed, v2::CommandError error) {
    generated::command_reply reply{}; reply.actual_instance_id = current_snapshot().instance; reply.reference = echoed;
    const auto state = result.execution.state;
    reply.dimensions.progress = state == SC_DIAGNOSTIC_QUEUED ? 0u : (state == SC_DIAGNOSTIC_CLAIMED ? 1u : (state >= SC_DIAGNOSTIC_EXECUTED ? 2u : 3u));
    reply.dimensions.execution = static_cast<uint32_t>(result.evidence.execution);
    reply.dimensions.effect = static_cast<uint32_t>(result.evidence.effect);
    reply.dimensions.retention = static_cast<uint32_t>(result.retention); reply.dimensions.result_token = result.result_token;
    if (error == v2::CommandError::effect_unknown || error == v2::CommandError::not_found) {
        reply.dimensions.execution = 2; reply.dimensions.effect = 4;
    }
    generated::Bytes out, bytes;
    generated::command_response envelope{reply, static_cast<uint32_t>(error), generated::to_wire(result.execution)};
    if (!generated::encode(envelope, out)) return {};
    if (result.retention == native::Diagnostics::Retention::full_result && state >= SC_DIAGNOSTIC_EXECUTED && echoed.command_type == 8) {
        if (!generated::encode(generated::diagnostic_detail_result{generated::to_wire(result.execution), generated::to_wire(result.detail)}, bytes)) return {};
    } else if (!v2::encode_native_result(result.value, bytes)) return {};
    append(out, bytes); return out;
}
WireResult submit_v2(const generated::Bytes& body, generated::Bytes& response) {
    generated::submission input; generated::Bytes payload;
    if (!v2::split_submission(body, input, payload)) return WireResult::malformed;
    auto echoed = reference(input.reference);
    native::Diagnostics::Reply result{};
    const auto* descriptor = registry::descriptor(echoed.command_type, echoed.payload_revision);
    generated::NativeRequest value;
    v2::CommandError error = v2::CommandError::none;
    if (!descriptor) error = v2::CommandError::unsupported_revision;
    else if (!v2::native_payload(*descriptor, payload, input, value, generated::Operation::submit)) error = v2::CommandError::malformed;
    else if (!v2::fingerprint(input, value, echoed.fingerprint)) error = v2::CommandError::malformed;
    else if (!v2_identity(echoed.scope)) error = v2::CommandError::effect_unknown;
    else {
        result = native::submit(input, value);
        error = static_cast<v2::CommandError>(result.error);
        if (error == v2::CommandError::none) echoed = result.reference;
    }
    response = command_body(result, echoed, error);
    return error == v2::CommandError::unsupported_revision ? WireResult::incompatible_protocol : WireResult::ok;
}
WireResult reference_v2(uint16_t operation, const generated::Bytes& body, generated::Bytes& response) {
    generated::command_reference ref; generated::ack input;
    const bool acknowledging = operation == 5;
    if (acknowledging) { if (!generated::decode(body, input)) return WireResult::malformed; ref = input.reference; }
    else if (!generated::decode(body, ref)) return WireResult::malformed;
    native::Diagnostics::Reply result{};
    auto error = v2::CommandError::effect_unknown;
    if (v2_identity(ref.scope)) {
        result = acknowledging ? native::ack(ref, input.result_token) : native::query(ref, operation == 4);
        error = static_cast<v2::CommandError>(result.error);
    }
    response = command_body(result, ref, error); return WireResult::ok;
}
WireResult read_v2(const generated::Bytes& body, generated::Bytes& response) {
    generated::read_envelope input; generated::Bytes payload;
    if (!v2::split_read(body, input, payload)) return WireResult::malformed;
    const auto* descriptor = registry::descriptor(input.command_type, input.payload_revision);
    if (!descriptor) return WireResult::incompatible_protocol;
    generated::submission envelope{{input.scope, input.namespace_id, "read", input.command_type, input.payload_revision}, input.save_binding, 1000};
    generated::NativeRequest request;
    if (!v2::native_payload(*descriptor, payload, envelope, request, generated::Operation::read)) return WireResult::unsupported_operation;
    if (!v2_identity(input.scope)) return WireResult::malformed;
    generated::Bytes bytes;
    if (!generated::encode(v2_scope(), response)) return WireResult::malformed;
    generated::NativeResult value;
    if (descriptor->execution == generated::Execution::snapshot) {
        const auto& policy = generated::admission_policies[static_cast<size_t>(descriptor->admission)];
        if (policy.save_binding == generated::BindingRule::exact && input.save_binding != save::session().binding_token())
            return WireResult::capability_unavailable;
        registry::ExecutionContext context{}; context.snapshot = registry_snapshot;
        registry::handler(static_cast<registry::Id>(input.command_type))->execute(request, value, context);
    }
    else {
        auto observed = native::read(envelope, request);
        if (observed.execution.state != SC_DIAGNOSTIC_EXECUTED) return WireResult::capability_unavailable;
        value = std::move(observed.value);
    }
    if (input.command_type == 12) {
        generated::save_admission_result result{generated::to_wire(std::get<sc_save_admission_snapshot>(value)), save::session().binding_token()};
        if (!generated::encode(result, bytes)) return WireResult::malformed;
    } else if (!v2::encode_native_result(value, bytes)) return WireResult::malformed;
    append(response, bytes); return WireResult::ok;
}
WireResult dispatch_v2(const generated::wire_header_2& header, const generated::Bytes& body, generated::Bytes& response) {
    switch (header.operation) {
    case 1: return read_v2(body, response);
    case 2: return submit_v2(body, response);
    case 3: case 4: case 5: return reference_v2(header.operation, body, response);
    case 6: {
        if (!body.empty()) return WireResult::malformed;
        generated::hello_result hello{v2_scope(), {}, 16384, 8, 30000, 65536};
        for (size_t i = 0; i < generated::descriptor_count; ++i)
            hello.supported.push_back({generated::descriptors[i].id, generated::descriptors[i].revision});
        return generated::encode(hello, response) ? WireResult::ok : WireResult::malformed;
    }
    default: return WireResult::unsupported_operation;
    }
}
}
bool process_v2(const generated::wire_header_2& input, const generated::Bytes& body, generated::Bytes& response) {
    generated::Bytes output;
    const auto result = dispatch_v2(input, body, output);
    if (input.operation == 1 && output.empty() && !generated::encode(v2_scope(), output)) return false;
    auto header = input; header.flags = 1; header.result = static_cast<uint32_t>(result);
    return v2::frame(header, output, response);
}
}
