// Copyright (c) 2026 snowzzrra. MIT; see ../LICENSE.
#include "save_recovery.h"
#include "save_session.h"
#include "save_sdk_write.h"
#include <windows.h>
#include <algorithm>
#include <cstring>
#include <map>

namespace sentinel::save {
namespace {
bool valid_recovery_record(const std::string&,const std::string&);
}
uint64_t current_steam_owner(uintptr_t remote) {
    // Do not initialize Steam, load a DLL or select a different account. The
    // native initializer uses this exact HSteamUser/interface pair (141bd87d0).
    const auto module = GetModuleHandleW(L"steam_api64.dll");
    if (!module || !remote) return 0;
    const auto user = reinterpret_cast<int32_t (*)()>(GetProcAddress(module,"SteamAPI_GetHSteamUser"));
    const auto find = reinterpret_cast<uintptr_t (*)(int32_t,const char*)>(GetProcAddress(module,"SteamInternal_FindOrCreateUserInterface"));
    const auto id = reinterpret_cast<uint64_t (*)(uintptr_t)>(GetProcAddress(module,"SteamAPI_ISteamUser_GetSteamID"));
    if (!user || !find || !id) return 0;
    const auto handle = user();
    if (!handle || find(handle,"STEAMREMOTESTORAGE_INTERFACE_VERSION014") != remote) return 0;
    const auto identity = find(handle,"SteamUser020");
    return identity ? id(identity) : 0;
}
namespace {
std::string hex(std::string_view bytes) {
    constexpr char chars[]="0123456789abcdef"; std::string out;
    for (unsigned char c:bytes) { out+=chars[c>>4]; out+=chars[c&15]; } return out;
}
std::string hash(const storage::TransportFile& f) {
    return hex(std::string_view(reinterpret_cast<const char*>(f.sha256.data()),f.sha256.size()));
}
bool take(std::string_view& in,std::string_view key,std::string_view& value) {
    if (in.substr(0,key.size())!=key) return false;
    in.remove_prefix(key.size()); const auto end=in.find('\n');
    if (end==in.npos) return false; value=in.substr(0,end); in.remove_prefix(end+1); return true;
}
bool payload_name(std::string_view name) {
    return name=="game.details" || name=="game_duration.dat" ||
        name=="game.details-BACKUP" || name=="game_duration.dat-BACKUP";
}
std::string canonical(std::string name) {
    for (auto& c:name) if (c>='A' && c<='Z') c=static_cast<char>(c+('a'-'A'));
    if (name.size()>7 && name.substr(name.size()-7)=="-backup") name.replace(name.size()-7,7,"-BACKUP");
    return name;
}
struct Checkpoint { uint64_t number=0; std::map<std::string,std::pair<uint32_t,std::string>> files; };
bool checkpoint(std::string_view value,const std::string& contract,const std::string& directory,Checkpoint& out) {
    storage::CheckpointRecord record;
    if (!storage::parse_checkpoint(value,contract,directory,record).ok() || record.files.size()>4) return false;
    out.number=record.number;
    for (const auto& file:record.files) {
        if (!payload_name(file.name) || !out.files.emplace(file.name,std::make_pair(file.size,hash(file))).second) return false;
    }
    return true;
}
bool matches(const Checkpoint& record,const storage::TransportMetadata& metadata) {
    if (record.files.size()!=metadata.files.size()) return false;
    for (const auto& f:metadata.files) {
        const auto found=record.files.find(f.name);
        if (found==record.files.end() || found->second!=std::make_pair(f.size,hash(f))) return false;
    }
    return true;
}
bool equal(const storage::TransportMetadata& a,const storage::TransportMetadata& b) {
    if (a.files.size()!=b.files.size()) return false;
    for (const auto& f:a.files) {
        const auto it=std::find_if(b.files.begin(),b.files.end(),[&](const auto& p){return p.name==f.name;});
        if (it==b.files.end() || f.size!=it->size || f.sha256!=it->sha256) return false;
    }
    return true;
}
bool same_checkpoint(const storage::TransportMetadata& current,const storage::TransportMetadata& source) {
    if (equal(current,source)) return true;
    if (source.files.size()!=2 || current.files.size()!=4) return false;
    auto primary=current;
    primary.files.erase(std::remove_if(primary.files.begin(),primary.files.end(),[](const auto& file) {
        return file.name=="game.details-BACKUP" || file.name=="game_duration.dat-BACKUP";
    }),primary.files.end());
    return equal(primary,source);
}
struct Payloads {
    std::vector<std::string> bytes;
    static bool read(void* source,size_t i,uint32_t offset,char* out,uint32_t count) {
        const auto& p=*static_cast<Payloads*>(source); std::memcpy(out,p.bytes[i].data()+offset,count); return true;
    }
};
bool read_remote_name(engine::Memory& memory, const char* key, std::string& name) {
    for (size_t n = 0; n < 260; ++n) {
        char c = 0;
        if (memory.copy(reinterpret_cast<uintptr_t>(key) + n, &c, 1).reason) return false;
        if (!c) break;
        name += c;
    }
    if (name.size() == 260) return false;
    return true;
}
bool read_remote_payload(const RecoveryTransport& t, engine::Memory& memory, const std::string& name, bool allow_empty,
                         uint64_t deadline, storage::TransportFile& file, std::string& bytes, int32_t reported) {
    const auto size = t.size(t.remote, name.c_str());
    if (size < 0 || size > 100 * 1024 * 1024 || reported != size) return false;
    if (!size && !allow_empty) return false;
    bytes.assign(static_cast<size_t>(size), '\0');
    if (t.read(t.remote, name.c_str(), bytes.data(), size) != size || t.size(t.remote, name.c_str()) != size)
        return false;
    file.size = static_cast<uint32_t>(size);
    if (!digest_payload(memory, reinterpret_cast<uintptr_t>(bytes.data()), file.size, file.sha256, deadline))
        return false;
    return true;
}
bool capture(const RecoveryTransport& t,const std::string& directory,storage::TransportMetadata& out,Payloads& payloads,bool allow_empty=false) {
    out.files.clear(); payloads.bytes.clear(); const auto count=t.count(t.remote);
    if (count<0 || count>100000) return false;
    const auto prefix=directory+"/"; const auto deadline=GetTickCount64()+10000;
    engine::LocalMemory memory;
    for (int32_t i=0;i<count;++i) {
        int32_t reported=0; const auto* key=t.name(t.remote,i,&reported);
        if (!key || GetTickCount64()>deadline) return false;
        std::string name;
        if (!read_remote_name(memory, key, name)) return false;
        if (!steam_name_equal(std::string_view(name).substr(0,prefix.size()),prefix)) continue;
        const auto relative=canonical(name.substr(prefix.size()));
        if (!payload_name(relative) || out.files.size()==4 || std::any_of(out.files.begin(),out.files.end(),[&](const auto& f){return f.name==relative;})) return false;
        storage::TransportFile file{relative, 0, {}};
        std::string bytes;
        if (!read_remote_payload(t, memory, name, allow_empty, deadline, file, bytes, reported)) return false;
        out.files.push_back(file); payloads.bytes.push_back(std::move(bytes));
    }
    return true;
}
}
bool snapshot_campaign_files(const RecoveryTransport& transport,const std::string& directory,storage::TransportMetadata& metadata) {
    if (!transport.remote || !transport.read || !transport.size || !transport.count || !transport.name) return false;
    Payloads payloads;
    return capture(transport,directory,metadata,payloads);
}
bool recovery_clear(storage::Namespace& lease,bool* cleared) {
    std::string pending,complete;
    const auto p=lease.recovery_record(false,pending),c=lease.recovery_record(true,complete);
    if (p.outcome==storage::Outcome::corrupt_manifest || c.outcome==storage::Outcome::corrupt_manifest ||
        (p.ok() && !valid_recovery_record(pending,lease.metadata().namespace_id)) ||
        (p.win32_error==ERROR_FILE_NOT_FOUND && c.ok())) {
        const bool ok=lease.clear_recovery_records().ok();
        if (cleared) *cleared=ok;
        return ok;
    }
    if (!p.ok()) return p.win32_error==ERROR_FILE_NOT_FOUND && !c.ok() && c.win32_error==ERROR_FILE_NOT_FOUND;
    return !pending.empty() && c.ok() && pending==complete;
}
bool preserve_recovery_auxiliaries(const storage::TransportMetadata& source, bool recorded, const Checkpoint& target,
                                   const storage::TransportMetadata& current, storage::TransportMetadata& tracked,
                                   storage::TransportMetadata& expected_after, RecoveryResult& result) {
    if (source.files.size() == 2 && recorded && target.files.size() == 2 && target.files.count("game.details") &&
        target.files.count("game_duration.dat")) {
        unsigned auxiliaries = 0;
        for (const auto& f : current.files)
            if (f.name == "game.details-BACKUP" || f.name == "game_duration.dat-BACKUP") {
                ++auxiliaries;
                expected_after.files.push_back(f);
            }
        if (auxiliaries == 1) return false;
        if (auxiliaries == 2) {
            tracked.files.erase(std::remove_if(tracked.files.begin(), tracked.files.end(),
                                               [](const auto& f) {
                                                   return f.name == "game.details-BACKUP" ||
                                                          f.name == "game_duration.dat-BACKUP";
                                               }),
                                tracked.files.end());
            result.preserved_auxiliaries = 2;
        }
    }
    return true;
}
bool recovery_checkpoint_matches(const storage::TransportMetadata& source, const std::string& contract,
                                 const std::string& directory, Checkpoint& saved) {
    if (!checkpoint(source.checkpoint, contract, directory, saved) || !matches(saved, source) ||
        !saved.files.count("game.details") || !saved.files.count("game_duration.dat") ||
        (saved.files.size() != 2 && saved.files.size() != 4))
        return false;
    return true;
}
static bool recovery_failure(RecoveryResult& result, const char* reason) { result.reason = reason; return false; }
static bool recovery_provider_available(const RecoveryTransport& t) {
    return t.remote && t.owner && t.write && t.read && t.exists && t.size && t.count && t.name;
}
struct RecoveryOwnership {
    const RecoveryTransport& t; uint64_t user; const std::string& marker; const std::string& ownership; const std::string& directory;
    bool matches() const {
        std::string bytes(ownership.size(), '\0');
        return t.owner(t.remote) == user && t.size(t.remote, marker.c_str()) == static_cast<int32_t>(bytes.size()) &&
            t.read(t.remote, marker.c_str(), bytes.data(), static_cast<int32_t>(bytes.size())) == static_cast<int32_t>(bytes.size()) && bytes == ownership;
    }
};
static bool validate_recovery_archive(const storage::Descriptor& descriptor, storage::Namespace& lease, const storage::TransportMetadata& source, uint64_t user, const std::string& contract, const std::string& directory, Checkpoint& saved, RecoveryResult& result) {
    std::string current_contract;
    if(!user || source.steam_user!=user || source.provider!=recovery_provider || source.quarantine) return recovery_failure(result, "recovery_native_user_or_provider_mismatch");
    if(descriptor.campaign.difficulty>3 || source.contract!=contract || source.directory!=directory ||
        !lease.campaign_record(false,current_contract).ok() || current_contract!=contract) return recovery_failure(result, "recovery_contract_or_options_mismatch");
    if (!recovery_checkpoint_matches(source, contract, directory, saved))
        return recovery_failure(result, "recovery_archive_checkpoint_mismatch");
    return true;
}
static bool read_recovery_selection(const RecoveryTransport& t, const std::string& selection_key, bool had_selection, std::string& previous_selection, RecoveryResult& result) {
    if(had_selection) {
        const auto length=t.size(t.remote,selection_key.c_str());
        if(length<=0 || length>2048) return recovery_failure(result, "recovery_selection_unavailable");
        previous_selection.resize(static_cast<size_t>(length));
        if(t.read(t.remote,selection_key.c_str(),previous_selection.data(),length)!=length) return recovery_failure(result, "recovery_selection_unavailable");
    }
    return true;
}
static bool recovery_subset_known(bool valid, bool recorded, const storage::TransportMetadata& tracked, const Checkpoint& target, RecoveryResult& result) {
    if(!valid && !tracked.files.empty()) {
        bool known_subset=recorded && tracked.files.size()<target.files.size();
        for(const auto& f:tracked.files) {
            const auto expected=target.files.find(f.name);
            known_subset=known_subset && expected!=target.files.end() && expected->second==std::make_pair(f.size,hash(f));
        }
        if(!known_subset) { result.state=RecoveryState::corrupt_unknown; return recovery_failure(result, "recovery_unknown_requires_conflict_decision"); }
    }
    return true;
}
static bool recovery_requires_write(const storage::TransportMetadata& source, const storage::TransportMetadata& tracked, bool recorded, const Checkpoint& target, const Checkpoint& saved, const std::string& current_record, RecoveryResult& result) {
    const bool valid=recorded && matches(target,tracked);
    const bool same=equal(source,tracked);
    if(recorded && target.number>saved.number) { result.state=RecoveryState::newer; return recovery_failure(result, "recovery_newer_recorded_progress"); }
    if(valid && same && current_record==source.checkpoint) {
        result.state=RecoveryState::same; result.complete=true; result.reason="recovery_already_exact"; return false;
    }
    if(valid && target.number==saved.number) { result.state=RecoveryState::conflict; return recovery_failure(result, "recovery_valid_ordering_conflict"); }
    result.state=valid?RecoveryState::older:tracked.files.size()<source.files.size()?RecoveryState::missing_incomplete:RecoveryState::corrupt_unknown;
    if (!recovery_subset_known(valid, recorded, tracked, target, result)) return false;
    // No deletion API: unknown/extra files are never swept away. A complete
    // source must cover every tracked target file before any write is possible.
    // Any untracked native auxiliary pair remains byte-identical in expected.
    for(const auto& f:tracked.files) if(std::none_of(source.files.begin(),source.files.end(),[&](const auto& s){return s.name==f.name;})) return recovery_failure(result, "recovery_target_extra_payload_conflict");
    return true;
}
static bool stage_recovery_payloads(storage::TransportArchive& archive, const storage::TransportMetadata& source, Payloads& replacement, RecoveryResult& result) {
    for(size_t i=0;i<source.files.size();++i) {
        std::string bytes(source.files[i].size,'\0');
        if(!archive.read(i,0,bytes.data(),source.files[i].size).ok()) return recovery_failure(result, "recovery_staging_failed");
        replacement.bytes.push_back(std::move(bytes));
    }
    return true;
}
static bool quarantine_recovery_target(storage::Namespace& lease, const storage::TransportMetadata& current, Payloads& before, RecoveryResult& result) {
    if(!current.files.empty()) {
        storage::Backup quarantine;
        auto evidence=current; evidence.quarantine=true; evidence.operation_id=0; evidence.process_id=GetCurrentProcessId();
        FILETIME created{},exit{},kernel{},cpu{};
        if(!GetProcessTimes(GetCurrentProcess(),&created,&exit,&kernel,&cpu)) return recovery_failure(result, "recovery_quarantine_process_identity_failed");
        evidence.process_created=(uint64_t(created.dwHighDateTime)<<32)|created.dwLowDateTime;
        if(!lease.backup_transport(evidence,Payloads::read,&before,quarantine).ok()) return recovery_failure(result, "recovery_quarantine_failed");
        result.quarantine=quarantine.path;
        const auto leaf=result.quarantine.substr(result.quarantine.find_last_of(L"\\/")+1);
        std::unique_ptr<storage::TransportArchive> verified;
        if(!lease.reopen_transport(leaf,verified).ok() || !equal(verified->metadata(),current)) return recovery_failure(result, "recovery_quarantine_verification_failed");
    }
    return true;
}
static bool recovery_target_unchanged(storage::Namespace& lease, const RecoveryOwnership& owned, const storage::TransportMetadata& source, const storage::TransportMetadata& current, const std::string& current_record, const storage::Result& r, bool ab, const storage::CheckpointPlan& ab_plan, const std::string& selection_key, bool had_selection, const std::string& previous_selection, RecoveryResult& result) {
    const auto& t = owned.t; const auto& directory = owned.directory;
    // Re-observe after staging/quarantine, still before the durable write-ahead
    // barrier. No native campaign job exists at this qualified startup boundary.
    storage::TransportMetadata check=source; Payloads check_bytes;
    std::string check_record;
    const auto checked=lease.campaign_record(true,check_record);
    if (ab && check_record=="sentinel-checkpoint-ab-v1\n") check_record=ab_plan.text;
    std::string selection_before(previous_selection.size(),'\0');
    if(!owned.matches() || !capture(t,directory,check,check_bytes) || !equal(check,current) ||
        (checked.ok()!=r.ok()) || check_record!=current_record || t.exists(t.remote,selection_key.c_str())!=had_selection ||
        (had_selection && (t.size(t.remote,selection_key.c_str())!=static_cast<int32_t>(previous_selection.size()) ||
            t.read(t.remote,selection_key.c_str(),selection_before.data(),static_cast<int32_t>(selection_before.size()))!=static_cast<int32_t>(selection_before.size()) ||
            selection_before!=previous_selection))) return recovery_failure(result, "recovery_concurrent_target_change");
    return true;
}
static bool write_recovery_payloads(const RecoveryOwnership& owned, const storage::TransportMetadata& source, const storage::TransportMetadata& expected_after, const Payloads& replacement, RecoveryResult& result) {
    const auto& t = owned.t; const auto& directory = owned.directory;
    storage::TransportMetadata check = source; Payloads check_bytes;
    for(size_t i=0;i<source.files.size();++i) {
        if(!owned.matches()) return recovery_failure(result, "recovery_provider_changed_during_transaction");
        const auto key=directory+"/"+source.files[i].name;
        result.mutated=true;
        if(!t.write(t.remote,key.c_str(),replacement.bytes[i].data(),static_cast<int32_t>(replacement.bytes[i].size()))) return recovery_failure(result, "recovery_native_write_failed");
    }
    if(!owned.matches() || !capture(t,directory,check,check_bytes) || !equal(expected_after,check)) return recovery_failure(result, "recovery_target_verification_failed");
    return true;
}
static bool write_recovery_selection(const RecoveryOwnership& owned, const std::string& id, const std::string& selection_key, RecoveryResult& result) {
    const auto& t = owned.t;
    const auto selection="sentinel-native-selection-v1\nnamespace_id="+id+"\ncampaign=GAME-\nname=AUTOSAVE0\n";
    if(!t.write(t.remote,selection_key.c_str(),selection.data(),static_cast<int32_t>(selection.size()))) return recovery_failure(result, "recovery_selection_failed");
    std::string selection_check(selection.size(),'\0');
    if(t.size(t.remote,selection_key.c_str())!=static_cast<int32_t>(selection.size()) ||
        t.read(t.remote,selection_key.c_str(),selection_check.data(),static_cast<int32_t>(selection.size()))!=static_cast<int32_t>(selection.size()) ||
        selection_check!=selection || !owned.matches()) return recovery_failure(result, "recovery_selection_verification_failed");
    return true;
}
static bool publish_recovery_checkpoint(storage::Namespace& lease, storage::CheckpointWrite& next, const storage::TransportMetadata& source, bool ab, const storage::CheckpointPlan& ab_plan, const storage::Result& r, RecoveryResult& result) {
    if (ab && source.checkpoint!=ab_plan.text) {
        if (!lease.receipt_checkpoint(next).ok() || !lease.commit_checkpoint(next).ok()) return recovery_failure(result, "recovery_continuity_publication_failed");
    } else if (!ab && !lease.publish_campaign_record(true,source.checkpoint,!r.ok()).ok()) return recovery_failure(result, "recovery_continuity_publication_failed");
    return true;
}
static bool open_recovery_archive(const storage::Descriptor& descriptor, storage::Namespace& lease, const RecoveryTransport& t, std::unique_ptr<storage::TransportArchive>& archive, uint64_t& user, RecoveryResult& result) {
    if (descriptor.campaign.intent!=storage::CampaignIntent::recover || descriptor.campaign.recovery_basename.empty()) return recovery_failure(result, "recovery_explicit_archive_required");
    if(!recovery_clear(lease,&result.record_cleared)) return recovery_failure(result, "recovery_previous_attempt_requires_review");
    if(!recovery_provider_available(t)) return recovery_failure(result, "recovery_provider_unavailable");
    user=t.owner(t.remote);
    const auto& basename=descriptor.campaign.recovery_basename;
    if(!lease.reopen_transport(std::wstring(basename.begin(),basename.end()),archive).ok()) return recovery_failure(result, "recovery_archive_verification_failed");
    return true;
}
static bool read_recovery_checkpoint(storage::Namespace& lease, const storage::Descriptor& descriptor, const storage::Result& r, std::string& current_record, bool& ab, storage::CheckpointPlan& ab_plan, RecoveryResult& result) {
    if(!r.ok() && r.win32_error!=ERROR_FILE_NOT_FOUND) return recovery_failure(result, "recovery_target_metadata_unavailable");
    ab=current_record=="sentinel-checkpoint-ab-v1\n";
    if (ab) {
        if (!lease.checkpoint_plan(descriptor,ab_plan).ok() || ab_plan.text.empty()) return recovery_failure(result, "recovery_target_metadata_unavailable");
        current_record=ab_plan.text;
    }
    return true;
}
struct RecoveryTarget {
    storage::TransportMetadata current, expected_after;
    Payloads before;
    std::string record;
    storage::Result status;
    storage::CheckpointPlan plan;
    bool ab = false;
};
static bool begin_recovery_checkpoint(storage::Namespace& lease, const storage::Descriptor& descriptor, uint64_t user,
        const Checkpoint& saved, const RecoveryTarget& target, const storage::TransportMetadata& source,
        storage::CheckpointWrite& next, RecoveryResult& result) {
    if (!target.ab || source.checkpoint==target.plan.text) return true;
    if (target.plan.checkpoint==UINT64_MAX || saved.number!=target.plan.checkpoint+1 ||
        !lease.begin_checkpoint(descriptor,user,next).ok() ||
        !lease.stage_checkpoint(next,std::wstring(descriptor.campaign.recovery_basename.begin(),descriptor.campaign.recovery_basename.end())).ok())
        return recovery_failure(result,"recovery_continuity_publication_failed");
    return true;
}
static bool inspect_recovery_target(const storage::Descriptor& descriptor, storage::Namespace& lease, const RecoveryTransport& t, const storage::TransportMetadata& source, const std::string& contract, const std::string& directory, const Checkpoint& saved, RecoveryTarget& state, RecoveryResult& result) {
    auto& current = state.current; auto& expected_after = state.expected_after; auto& before = state.before;
    auto& current_record = state.record; auto& r = state.status; auto& ab_plan = state.plan; auto& ab = state.ab;
    current = source; expected_after = source;
    if(!capture(t,directory,current,before)) return recovery_failure(result, "recovery_target_inventory_unavailable_or_unknown");
    r=lease.campaign_record(true,current_record);
    if (!read_recovery_checkpoint(lease, descriptor, r, current_record, ab, ab_plan, result)) return false;
    Checkpoint target;
    const bool recorded=checkpoint(current_record,contract,directory,target);
    // A normal retail write owns the primary pair. Native rotation can leave
    // a separate BACKUP pair which that operation/continuity never claimed.
    // Keep these opaque bytes: quarantine and recheck the FULL inventory, but
    // classify/restore only the operation's files. Never infer backup validity.
    auto tracked=current;
    if (!preserve_recovery_auxiliaries(source, recorded, target, current, tracked, expected_after, result))
        return recovery_failure(result, "recovery_auxiliary_pair_incomplete");
    if (!recovery_requires_write(source, tracked, recorded, target, saved, current_record, result)) return false;
    return true;
}
static std::string recovery_journal(const std::string& id, const std::string& basename, const std::wstring& quarantine, const std::string& previous_record, bool had_selection, const std::string& previous_selection) {
    const auto leaf=quarantine.substr(quarantine.find_last_of(L"\\/")+1);
    std::string quarantine_basename; for(const auto c:leaf) quarantine_basename+=static_cast<char>(c);
    const auto journal="sentinel-recovery-v1\nnamespace="+id+"\nbackup="+basename+"\nquarantine="+quarantine_basename+
        "\nprevious_checkpoint_hex="+hex(previous_record)+"\nprevious_selection_present="+(had_selection?"1":"0")+
        "\nprevious_selection_hex="+hex(previous_selection)+"\n";
    return journal;
}
RecoveryResult recover_campaign(storage::Namespace& lease,const storage::Descriptor& descriptor,const RecoveryTransport& t) {
    RecoveryResult result;
    const auto refuse=[&](const char* why) { result.reason=why; return result; };
    std::unique_ptr<storage::TransportArchive> archive; uint64_t user = 0;
    if (!open_recovery_archive(descriptor, lease, t, archive, user, result)) return result;
    const auto& basename = descriptor.campaign.recovery_basename;
    const auto& source=archive->metadata();
    const auto id=lease.metadata().namespace_id;
    const auto directory=lease.metadata().native_root+"/GAME-AUTOSAVE0";
    const auto contract=campaign_contract(descriptor,id);
    Checkpoint saved;
    if (!validate_recovery_archive(descriptor, lease, source, user, contract, directory, saved, result)) return result;
    const auto marker=lease.metadata().native_root+"/sentinel-owner-"+id+".txt";
    const auto& identity=descriptor.identity;
    const auto ownership="sentinel-native-session-v1\nnamespace_id="+id+"\nseed_hex="+hex(identity.seed)+"\nteam="+std::to_string(*identity.team)+
        "\nslot="+std::to_string(*identity.slot)+"\ngeneration_fingerprint="+identity.generation_fingerprint+"\nprovenance=synthetic-fixture\n";
    RecoveryOwnership owned{t, user, marker, ownership, directory};
    if(!owned.matches()) return refuse("recovery_native_marker_mismatch");
    const auto selection_key=lease.metadata().native_root+"/sentinel-selection-GAME.txt";
    std::string previous_selection;
    const bool had_selection=t.exists(t.remote,selection_key.c_str());
    if (!read_recovery_selection(t, selection_key, had_selection, previous_selection, result)) return result;
    RecoveryTarget target;
    if (!inspect_recovery_target(descriptor, lease, t, source, contract, directory, saved, target, result)) return result;
    Payloads replacement;
    if (!stage_recovery_payloads(*archive, source, replacement, result)) return result;
    target.current.checkpoint=target.record;
    if (!quarantine_recovery_target(lease, target.current, target.before, result)) return result;
    const auto journal = recovery_journal(id, basename, result.quarantine, target.record, had_selection, previous_selection);
    if (!recovery_target_unchanged(lease, owned, source, target.current, target.record, target.status, target.ab, target.plan, selection_key, had_selection, previous_selection, result)) return result;
    storage::CheckpointWrite next;
    if (!begin_recovery_checkpoint(lease,descriptor,user,saved,target,source,next,result)) return result;
    if(!lease.publish_recovery_record(false,journal,true).ok()) return refuse("recovery_journal_failed");
    if (!write_recovery_payloads(owned, source, target.expected_after, replacement, result)) return result;
    if (!write_recovery_selection(owned, id, selection_key, result)) return result;
    if (!publish_recovery_checkpoint(lease, next, source, target.ab, target.plan, target.status, result)) return result;
    if(!lease.publish_recovery_record(true,journal,true).ok()) return refuse("recovery_completion_publication_failed");
    result.complete=true; result.reason="recovery_native_payload_verified"; return result;
}
namespace {
std::string digest_text(const std::string& bytes) {
    engine::LocalMemory memory; std::array<unsigned char,32> hash{};
    if (!digest_payload(memory,reinterpret_cast<uintptr_t>(bytes.data()),static_cast<uint32_t>(bytes.size()),hash,GetTickCount64()+10000)) return {};
    return hex(std::string_view(reinterpret_cast<const char*>(hash.data()),hash.size()));
}
std::string sign_journal(const std::string& body) { return body+"sha256="+digest_text(body)+"\n"; }
bool journal_selection(std::string_view& body, bool& selection_present, std::string& selection) {
    std::string_view part;
    if (!take(body, "selection_present=", part) || (part != "0" && part != "1")) return false;
    selection_present = part == "1";
    if (!take(body, "selection_hex=", part) || part.size() > 4096 || part.size() % 2 ||
        part.find_first_not_of("0123456789abcdef") != part.npos)
        return false;
    selection.clear();
    const auto digit = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
    for (size_t i = 0; i < part.size(); i += 2)
        selection += static_cast<char>((digit(part[i]) << 4) | digit(part[i + 1]));
    if (!selection_present && !selection.empty()) return false;
    return true;
}
bool journal_action(std::string_view& body, std::string& action, std::string& discard) {
    std::string_view part;
    if (!take(body, "action=", part) || (part != "commit" && part != "rollback")) return false;
    action = part;
    if (!take(body, "discard=", part) || (part != "A" && part != "B" && part != "none")) return false;
    discard = part;
    return true;
}
bool journal_fields(const std::string& journal,const std::string& id,std::string& source,std::string& quarantine,std::string& action,std::string& discard,
        bool& selection_present,std::string& selection) {
    std::string_view body(journal),part;
    if (!take(body,"sentinel-checkpoint-recovery-v1",part) || !part.empty() ||
        !take(body,"namespace=",part) || part!=id || !take(body,"source=",part)) return false;
    source=part;
    if (!take(body,"quarantine=",part)) return false; quarantine=part;
    if (!journal_selection(body, selection_present, selection)) return false;
    if (!journal_action(body, action, discard)) return false;
    if (!take(body,"sha256=",part) || !body.empty() || journal.size()<72) return false;
    return sign_journal(journal.substr(0,journal.size()-72))==journal;
}
bool recovery_hex_field(std::string_view& body,std::string_view key) {
    std::string_view part;
    return take(body,key,part) && part.size()%2==0 && part.find_first_not_of("0123456789abcdef")==part.npos;
}
bool legacy_recovery_fields(const std::string& text,const std::string& id) {
    std::string_view body(text),part;
    if (!take(body,"sentinel-recovery-v1",part) || !part.empty() ||
        !take(body,"namespace=",part) || part!=id || !take(body,"backup=",part) || part.empty() ||
        !take(body,"quarantine=",part) || !recovery_hex_field(body,"previous_checkpoint_hex=")) return false;
    if (!take(body,"previous_selection_present=",part) || (part!="0" && part!="1") ||
        !recovery_hex_field(body,"previous_selection_hex=") || !body.empty()) return false;
    return true;
}
bool valid_recovery_record(const std::string& text,const std::string& id) {
    std::string source,quarantine,action,discard,selection;
    bool present=false;
    return journal_fields(text,id,source,quarantine,action,discard,present,selection) || legacy_recovery_fields(text,id);
}
bool known_target(const storage::TransportMetadata& current,const Payloads& bytes,storage::TransportArchive& source,storage::TransportArchive* before) {
    for (size_t i=0;i<current.files.size();++i) {
        const auto& file=current.files[i];
        const auto matches_file=[&](const storage::TransportMetadata& set) {
            return std::any_of(set.files.begin(),set.files.end(),[&](const auto& f){ return f.name==file.name && f.size==file.size && f.sha256==file.sha256; });
        };
        if (matches_file(source.metadata()) || (before && matches_file(before->metadata()))) continue;
        const auto partial=[&](storage::TransportArchive& archive) {
            const auto& files=archive.metadata().files;
            for (size_t n=0;n<files.size();++n) if (files[n].name==file.name && file.size<files[n].size) {
                std::string prefix(file.size,'\0');
                return archive.read(n,0,prefix.data(),file.size).ok() && prefix==bytes.bytes[i];
            }
            return false;
        };
        if (!partial(source)) return false;
    }
    return true;
}
}
namespace {
struct CheckpointRestore {
    storage::Namespace& lease;
    const storage::Descriptor& descriptor;
    const RecoveryTransport& t;
    RecoveryResult result;
    uint64_t user = 0;
    std::string id, directory, contract, marker, ownership, selection_key, selection, prior_selection;
    bool had_selection = false, resuming = false, journal_selection_present = false;
    std::string journal, complete, source_name, quarantine_name, action, discard, journal_selection;
    storage::Result pending, finished;
    storage::CheckpointPlan plan;
    storage::TransportMetadata current, expected;
    Payloads before;
    std::unique_ptr<storage::TransportArchive> source_archive, quarantine_archive;
    bool refuse(const char* why) {
        result.reason = why;
        return false;
    }
    bool owned() {
        std::string bytes(ownership.size(),'\0');
        return user && t.owner(t.remote)==user && t.size(t.remote,marker.c_str())==static_cast<int32_t>(bytes.size()) &&
            t.read(t.remote,marker.c_str(),bytes.data(),static_cast<int32_t>(bytes.size()))==static_cast<int32_t>(bytes.size()) && bytes==ownership;
    }
    bool provider_ready() {
        if (!t.remote || !t.owner || !t.write || !t.read || !t.exists || !t.size || !t.count || !t.name)
            return refuse("recovery_provider_unavailable");
        user = t.owner(t.remote);
        id = lease.metadata().namespace_id;
        directory = lease.metadata().native_root + "/GAME-AUTOSAVE0";
        contract = campaign_contract(descriptor, id);
        const auto& identity = descriptor.identity;
        marker = lease.metadata().native_root + "/sentinel-owner-" + id + ".txt";
        ownership = "sentinel-native-session-v1\nnamespace_id=" + id + "\nseed_hex=" + hex(identity.seed) +
                    "\nteam=" + std::to_string(*identity.team) + "\nslot=" + std::to_string(*identity.slot) +
                    "\ngeneration_fingerprint=" + identity.generation_fingerprint + "\nprovenance=synthetic-fixture\n";
        if (!owned()) return refuse("recovery_native_marker_or_user_mismatch");
        return true;
    }
    bool read_selection() {
        selection_key = lease.metadata().native_root + "/sentinel-selection-GAME.txt";
        selection = "sentinel-native-selection-v1\nnamespace_id=" + id + "\ncampaign=GAME-\nname=AUTOSAVE0\n";
        had_selection = t.exists(t.remote, selection_key.c_str());
        if (had_selection) {
            const auto size = t.size(t.remote, selection_key.c_str());
            if (size < 0 || size > 2048) return refuse("recovery_selection_unavailable");
            prior_selection.resize(static_cast<size_t>(size));
            if (t.read(t.remote, selection_key.c_str(), prior_selection.data(), size) != size)
                return refuse("recovery_selection_unavailable");
        }
        return true;
    }
    bool read_plan() {
        auto r = lease.checkpoint_plan(descriptor, plan);
        if (!r.ok() || plan.state == storage::CheckpointState::identity_mismatch ||
            plan.state == storage::CheckpointState::conflict || plan.state == storage::CheckpointState::unsupported)
            return refuse("recovery_checkpoint_conflict");
        current.directory = directory;
        current.steam_user = user;
        current.provider = recovery_provider;
        current.contract = contract;
        current.process_id = GetCurrentProcessId();
        FILETIME created{}, exit{}, kernel{}, cpu{};
        if (!GetProcessTimes(GetCurrentProcess(), &created, &exit, &kernel, &cpu))
            return refuse("recovery_process_identity_failed");
        current.process_created = (uint64_t(created.dwHighDateTime) << 32) | created.dwLowDateTime;
        if (!lease.campaign_record(true, current.checkpoint).ok() || !capture(t, directory, current, before, true))
            return refuse("recovery_target_inventory_unavailable_or_unknown");
        return true;
    }
    bool preserve(std::string& basename) {
        if (current.files.empty()) { storage::Backup local; return lease.backup_offline(local).ok(); }
        auto evidence=current; evidence.quarantine=true; evidence.operation_id=0;
        storage::Backup copy;
        if (!lease.backup_transport(evidence,Payloads::read,&before,copy).ok()) return false;
        result.quarantine=copy.path; const auto leaf=copy.path.substr(copy.path.find_last_of(L"\\/")+1);
        for (const auto c:leaf) basename+=static_cast<char>(c);
        std::unique_ptr<storage::TransportArchive> verified;
        return lease.reopen_transport(leaf,verified).ok() && equal(current,verified->metadata());
    }
    bool commit_migration(const storage::Backup& copy) {
        storage::CheckpointWrite write;
        if (!lease.begin_checkpoint(descriptor, user, write).ok() ||
            !lease.stage_checkpoint(write, copy.path.substr(copy.path.find_last_of(L"\\/") + 1)).ok() ||
            !lease.receipt_checkpoint(write).ok() || !lease.commit_checkpoint(write).ok() ||
            !lease.activate_checkpoints(plan.text).ok())
            return refuse("checkpoint_migration_failed");
        return true;
    }
    bool migrate_legacy() {
        Checkpoint legacy;
        if (!checkpoint(plan.text,contract,directory,legacy) || !matches(legacy,current)) {
            std::string quarantine; if (!preserve(quarantine)) return refuse("recovery_preservation_failed");
            return refuse("recovery_legacy_checkpoint_unproven");
        }
        current.checkpoint=plan.text; current.operation_id=legacy.number; current.automatic=true;
        storage::Backup copy;
        if (!lease.backup_transport(current,Payloads::read,&before,copy).ok()) return refuse("checkpoint_migration_archive_failed");
        storage::TransportMetadata check=current; Payloads checked;
        if (!owned() || !capture(t,directory,check,checked,true) || !equal(current,check)) return refuse("recovery_concurrent_target_change");
        if (!commit_migration(copy)) return false;
        lease.prune_checkpoints();
        result.complete = true;
        result.reason = "checkpoint_migrated";
        return true;
    }
    bool check_selection() {
        if (resuming && had_selection && prior_selection != selection &&
            (!journal_selection_present || prior_selection != journal_selection) &&
            prior_selection != selection.substr(0, prior_selection.size()) &&
            (!journal_selection_present || prior_selection != journal_selection.substr(0, prior_selection.size())))
            return refuse("recovery_selection_changed");
        if (resuming && !had_selection && journal_selection_present) return refuse("recovery_selection_changed");
        return true;
    }
    bool read_journal_records() {
        pending = lease.recovery_record(false, journal);
        finished = lease.recovery_record(true, complete);
        if (pending.outcome==storage::Outcome::corrupt_manifest || finished.outcome==storage::Outcome::corrupt_manifest ||
            (pending.ok() && !valid_recovery_record(journal,id)) ||
            (pending.win32_error==ERROR_FILE_NOT_FOUND && finished.ok())) {
            if (!recovery_clear(lease,&result.record_cleared)) return refuse("recovery_journal_cleanup_failed");
            pending={storage::Outcome::io_error,ERROR_FILE_NOT_FOUND};
            journal.clear(); complete.clear(); resuming=false;
        }
        return true;
    }
    bool read_journal() {
        if (!read_journal_records()) return false;
        resuming = pending.ok() && (!finished.ok() || journal != complete) &&
                   journal_fields(journal, id, source_name, quarantine_name, action, discard, journal_selection_present,
                                   journal_selection);
        if (!pending.ok() && pending.win32_error != ERROR_FILE_NOT_FOUND &&
            pending.outcome != storage::Outcome::corrupt_manifest)
            return refuse("recovery_journal_unavailable");
        if (pending.ok() && journal != complete && !resuming)
            return refuse("recovery_journal_unknown");
        if (!resuming) {
            source_name = plan.archive;
            quarantine_name.clear();
            action.clear();
            discard.clear();
        }
        if (resuming && source_name != plan.archive && source_name != plan.previous_archive)
            return refuse("recovery_journal_source_conflict");
        return check_selection();
    }
    bool open_source() {
        return lease.reopen_transport(std::wstring(source_name.begin(), source_name.end()), source_archive).ok() &&
               source_archive->metadata().steam_user == user &&
               source_archive->metadata().provider == recovery_provider &&
               source_archive->metadata().contract == contract && source_archive->metadata().directory == directory &&
               !source_archive->metadata().quarantine;
    }
    bool choose_source() {
        if (source_name.empty()) {
            std::string quarantine;
            if (!preserve(quarantine)) return refuse("recovery_preservation_failed");
            return refuse("recovery_no_confirmed_predecessor");
        }
        if (!open_source()) return refuse("recovery_archive_verification_failed");
        return true;
    }
    bool choose_action() {
        if (!plan.committed && !same_checkpoint(current,source_archive->metadata())) {
            if (plan.previous_archive.empty()) { std::string name; preserve(name); return refuse("recovery_no_confirmed_predecessor"); }
            source_name=plan.previous_archive;
            if (!open_source()) return refuse("recovery_previous_archive_verification_failed");
            action="rollback"; discard=std::string(1,plan.side);
        } else {
            action=plan.committed?"rollback":"commit";
            discard=plan.committed && plan.state!=storage::CheckpointState::confirmed ? std::string(1,plan.side=='A'?'B':'A') : "none";
        }
        return true;
    }
    bool selection_unchanged() {
        std::string selected(prior_selection.size(), '\0');
        if (t.exists(t.remote, selection_key.c_str()) != had_selection ||
            (had_selection && (t.size(t.remote, selection_key.c_str()) != static_cast<int32_t>(selected.size()) ||
                               t.read(t.remote, selection_key.c_str(), selected.data(),
                                      static_cast<int32_t>(selected.size())) != static_cast<int32_t>(selected.size()) ||
                               selected != prior_selection)))
            return false;
        return true;
    }
    bool publish_journal() {
        journal = sign_journal(
            "sentinel-checkpoint-recovery-v1\nnamespace=" + id + "\nsource=" + source_name +
            "\nquarantine=" + quarantine_name + "\nselection_present=" + (had_selection ? "1" : "0") +
            "\nselection_hex=" + hex(prior_selection) + "\naction=" + action + "\ndiscard=" + discard + "\n");
        storage::TransportMetadata check = current;
        Payloads checked;
        if (!owned() || !capture(t, directory, check, checked, true) || !equal(current, check))
            return refuse("recovery_concurrent_target_change");
        if (!selection_unchanged()) return refuse("recovery_selection_changed");
        if (!lease.publish_recovery_record(false, journal, true).ok()) return refuse("recovery_journal_failed");
        return true;
    }
    bool prepare_recovery() {
        if (plan.state==storage::CheckpointState::confirmed && same_checkpoint(current,source_archive->metadata()) &&
            (!pending.ok() || journal==complete)) {
            lease.prune_checkpoints();
            result.complete = true;
            result.reason = "checkpoint_verified";
            return false;
        }
        if (!same_checkpoint(current,source_archive->metadata()) && !plan.interrupted_write && plan.committed) {
            std::string name; if (!preserve(name)) return refuse("recovery_preservation_failed");
            return refuse("recovery_external_payload_change");
        }
        if (!preserve(quarantine_name)) return refuse("recovery_preservation_failed");
        return publish_journal();
    }
    bool validate_archives() {
        if (!quarantine_name.empty() &&
            (!lease.reopen_transport(std::wstring(quarantine_name.begin(), quarantine_name.end()), quarantine_archive)
                  .ok() ||
             !quarantine_archive->metadata().quarantine || quarantine_archive->metadata().steam_user != user ||
             quarantine_archive->metadata().contract != contract))
            return refuse("recovery_preservation_verification_failed");
        if (resuming && !known_target(current, before, *source_archive, quarantine_archive.get()))
            return refuse("recovery_concurrent_target_change");
        return true;
    }
    bool expected_files() {
        const auto& source = source_archive->metadata();
        expected = source;
        for (const auto& file : current.files)
            if (std::none_of(source.files.begin(), source.files.end(),
                             [&](const auto& f) { return f.name == file.name; })) {
                if (file.name != "game.details-BACKUP" && file.name != "game_duration.dat-BACKUP")
                    return refuse("recovery_target_extra_payload_conflict");
                expected.files.push_back(file);
                ++result.preserved_auxiliaries;
            }
        if (result.preserved_auxiliaries == 1) return refuse("recovery_auxiliary_pair_incomplete");
        return true;
    }
    bool restore_files() {
        const auto& source = source_archive->metadata();
        if (action == "rollback" && !equal(current, expected)) {
            for (size_t i = 0; i < source.files.size(); ++i) {
                if (!owned()) return refuse("recovery_provider_changed_during_transaction");
                std::string bytes(source.files[i].size, '\0');
                if (!source_archive->read(i, 0, bytes.data(), source.files[i].size).ok())
                    return refuse("recovery_staging_failed");
                result.mutated = true;
                if (!t.write(t.remote, (directory + "/" + source.files[i].name).c_str(), bytes.data(),
                             static_cast<int32_t>(bytes.size())))
                    return refuse("recovery_native_write_failed");
            }
        }
        return true;
    }
    bool verify_target() {
        const auto& source = source_archive->metadata();
        storage::TransportMetadata check = source;
        Payloads checked;
        if (!owned() || !capture(t, directory, check, checked) || !equal(expected, check))
            return refuse("recovery_target_verification_failed");
        if (!t.write(t.remote, selection_key.c_str(), selection.data(), static_cast<int32_t>(selection.size())))
            return refuse("recovery_selection_failed");
        std::string selection_check(selection.size(), '\0');
        if (!owned() || t.size(t.remote, selection_key.c_str()) != static_cast<int32_t>(selection.size()) ||
            t.read(t.remote, selection_key.c_str(), selection_check.data(), static_cast<int32_t>(selection.size())) !=
                static_cast<int32_t>(selection.size()) ||
            selection_check != selection)
            return refuse("recovery_selection_verification_failed");
        return true;
    }
    bool finish_transaction() {
        if (action == "commit") {
            storage::CheckpointWrite write;
            write.side = plan.side;
            write.pending_hash = plan.pending_hash;
            write.receipt_hash = plan.receipt_hash;
            if (plan.archive != source_name || !lease.commit_checkpoint(write).ok())
                return refuse("recovery_commit_failed");
        }
        if (discard != "none" && !lease.discard_checkpoint(discard[0]).ok())
            return refuse("recovery_inactive_cleanup_failed");
        if (!lease.publish_recovery_record(true, journal, true).ok())
            return refuse("recovery_completion_publication_failed");
        lease.prune_checkpoints();
        result.complete = true;
        result.state = action == "commit" ? RecoveryState::same : RecoveryState::older;
        result.reason = action == "commit" ? "interrupted_save_recovered" : "previous_checkpoint_restored";
        return true;
    }
    RecoveryResult run() {
        if (!provider_ready() || !read_selection() || !read_plan()) return result;
        if (!plan.ab) {
            migrate_legacy();
            return result;
        }
        if (!read_journal() || !choose_source()) return result;
        if (!resuming && (!choose_action() || !prepare_recovery())) return result;
        if (!validate_archives() || !expected_files() || !restore_files() || !verify_target()) return result;
        finish_transaction();
        return result;
    }
};
} // namespace
RecoveryResult recover_checkpoint(storage::Namespace& lease, const storage::Descriptor& descriptor,
                                  const RecoveryTransport& t) {
    CheckpointRestore recovery{lease, descriptor, t};
    return recovery.run();
}

namespace {
void recover_assign(storage::Namespace& lease,const storage::Descriptor& descriptor,const RecoveryTransport& transport,RecoveryResult& result) {
    result=descriptor.campaign.intent==storage::CampaignIntent::recover ? recover_campaign(lease,descriptor,transport) : recover_checkpoint(lease,descriptor,transport);
}
void recover_guarded(storage::Namespace& lease,const storage::Descriptor& descriptor,const RecoveryTransport& transport,RecoveryResult& result) {
    __try { recover_assign(lease,descriptor,transport,result); }
    __except(EXCEPTION_EXECUTE_HANDLER) { result.complete=false; result.reason="recovery_native_memory_fault"; }
}
}
bool Session::recover_startup(const RecoveryTransport& transport) {
    if(!recovery_requested()) return true;
    btrace.record(BStage::checkpoint_recovery,BStatus::pending,"checkpoint_recovery_started");
    if(!acquire_native_root_lock() || routed() || !lease_) {
        btrace.record(BStage::checkpoint_recovery,BStatus::refused,"recovery_exclusive_target_unavailable");
        btrace.record(BStage::resume,BStatus::refused,"recovery_exclusive_target_unavailable",0,
            {{"classification",RecoveryState::unavailable},{"mutated",false}});
        fail(SessionFault::native_campaign); return false;
    }
    RecoveryResult result; recover_guarded(*lease_,recovery_descriptor_,transport,result);
    if(result.complete) {
        auto descriptor=recovery_descriptor_; descriptor.campaign.intent=storage::CampaignIntent::resume;
        if(campaign_run.enabled() ? !campaign_run.refresh_checkpoint() : !campaign_run.configure(*this,descriptor,*lease_)) {
            btrace.record(BStage::checkpoint_recovery,BStatus::refused,"recovery_resume_refused"); return false;
        }
    }
    btrace.record(BStage::resume,result.complete?BStatus::succeeded:BStatus::refused,result.reason,0,
        {{"classification",result.state},{"mutated",result.mutated},{"complete",result.complete},{"quarantined",!result.quarantine.empty()},
         {"preserved_auxiliaries",result.preserved_auxiliaries},{"record_cleared",result.record_cleared}});
    btrace.record(BStage::checkpoint_recovery,result.complete?BStatus::succeeded:BStatus::refused,result.reason,0,
        {{"classification",result.state},{"mutated",result.mutated},{"complete",result.complete},{"quarantined",!result.quarantine.empty()}});
    if(!result.complete) { fail(SessionFault::native_campaign); return false; }
    recovery_descriptor_.campaign.intent=storage::CampaignIntent::none;
    return true;
}
}
