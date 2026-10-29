#pragma once
#include "sentinel_protocol_generated.h"
#include "save_submission.h"

namespace sentinel::registry {
using Id = generated::CommandId;
using Request = generated::NativeRequest;
using Result = generated::NativeResult;
enum class ExecutionEvidence { not_entered, entered, unknown };
enum class EffectEvidence { no_effect, observed_effect, dispatch_only, accepted_publication, effect_unknown };
struct Evidence {
    ExecutionEvidence execution = ExecutionEvidence::not_entered;
    EffectEvidence effect = EffectEvidence::no_effect;
};
struct LiveState {
    sc_native_scope scope{};
    uint32_t process_reason = 0, gameplay_reason = 0;
    bool save_admitted = false;
    std::array<uint8_t, 16> save_binding{};
    std::string namespace_id;
};
struct ExecutionContext {
    uintptr_t player = 0;
    std::shared_ptr<save::BackupJob> backup;
    const char* backup_directory = nullptr;
    save::SubmissionResult submission{};
    Result (*snapshot)(Id, const Request&) = nullptr;
};
struct Handler {
    Id id;
    uint32_t (*admit)(const Request&, const LiveState&);
    void (*execute)(const Request&, Result&, ExecutionContext&);
    Evidence (*classify)(const Request&, const Result&, const sc_diagnostic_result&);
};
inline const generated::Descriptor* descriptor(uint16_t id, uint16_t revision) {
    for (size_t i = 0; i < generated::descriptor_count; ++i) {
        const auto& entry = generated::descriptors[i];
        if (entry.id == id && entry.revision == revision) return &entry;
    }
    return nullptr;
}
const Handler* handler(Id id);
size_t handler_count();
}
