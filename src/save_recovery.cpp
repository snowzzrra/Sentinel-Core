// Copyright (c) 2026 snowzzrra. MIT; see ../LICENSE.
#include "save_recovery.h"
#include "save_session.h"
#include "save_sdk_write.h"
#include <windows.h>
#include <algorithm>
#include <charconv>
#include <cstring>
#include <map>

namespace sentinel::save {
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
bool number(std::string_view value,uint64_t& out) {
    if (value.empty() || (value.size()>1 && value[0]=='0')) return false;
    const auto r=std::from_chars(value.data(),value.data()+value.size(),out);
    return r.ec==std::errc{} && r.ptr==value.data()+value.size();
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
    if (value.substr(0,contract.size())!=contract) return false;
    value.remove_prefix(contract.size()); std::string_view part; uint64_t count=0;
    if (!take(value,"checkpoint=",part) || !number(part,out.number) || !out.number ||
        !take(value,"map=",part) || part.empty() || part.size()>191 ||
        part.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_/.-")!=part.npos ||
        !take(value,"files=",part) || !number(part,count) || !count || count>4) return false;
    for (uint64_t i=0;i<count;++i) {
        if (!take(value,"file=",part)) return false;
        const auto first=part.find(','),last=part.rfind(','); uint64_t size=0;
        if (first==part.npos || first==last || part.substr(0,directory.size()+1)!=directory+"/" ||
            !number(part.substr(first+1,last-first-1),size) || !size || size>100u*1024u*1024u ||
            part.size()-last-1!=64 || part.substr(last+1).find_first_not_of("0123456789abcdef")!=part.npos) return false;
        const std::string name(part.substr(directory.size()+1,first-directory.size()-1));
        if (!payload_name(name) || !out.files.emplace(name,std::make_pair(static_cast<uint32_t>(size),std::string(part.substr(last+1)))).second) return false;
    }
    return value=="state=native_saved_readback_verified\n";
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
struct Payloads {
    std::vector<std::string> bytes;
    static bool read(void* source,size_t i,uint32_t offset,char* out,uint32_t count) {
        const auto& p=*static_cast<Payloads*>(source); std::memcpy(out,p.bytes[i].data()+offset,count); return true;
    }
};
bool capture(const RecoveryTransport& t,const std::string& directory,storage::TransportMetadata& out,Payloads& payloads) {
    out.files.clear(); payloads.bytes.clear(); const auto count=t.count(t.remote);
    if (count<0 || count>100000) return false;
    const auto prefix=directory+"/"; const auto deadline=GetTickCount64()+10000;
    engine::LocalMemory memory;
    for (int32_t i=0;i<count;++i) {
        int32_t reported=0; const auto* key=t.name(t.remote,i,&reported);
        if (!key || GetTickCount64()>deadline) return false;
        std::string name;
        for (size_t n=0;n<260;++n) { char c=0; if(memory.copy(reinterpret_cast<uintptr_t>(key)+n,&c,1).reason) return false; if(!c) break; name+=c; }
        if (name.size()==260) return false;
        if (!steam_name_equal(std::string_view(name).substr(0,prefix.size()),prefix)) continue;
        const auto relative=canonical(name.substr(prefix.size()));
        if (!payload_name(relative) || out.files.size()==4 || std::any_of(out.files.begin(),out.files.end(),[&](const auto& f){return f.name==relative;})) return false;
        const auto size=t.size(t.remote,name.c_str());
        if (size<=0 || size>100*1024*1024 || reported!=size) return false;
        std::string bytes(static_cast<size_t>(size),'\0');
        if(t.read(t.remote,name.c_str(),bytes.data(),size)!=size || t.size(t.remote,name.c_str())!=size) return false;
        storage::TransportFile file{relative,static_cast<uint32_t>(size),{}};
        if(!digest_payload(memory,reinterpret_cast<uintptr_t>(bytes.data()),file.size,file.sha256,deadline)) return false;
        out.files.push_back(file); payloads.bytes.push_back(std::move(bytes));
    }
    return true;
}
}
bool recovery_clear(storage::Namespace& lease) {
    std::string pending,complete;
    const auto p=lease.recovery_record(false,pending),c=lease.recovery_record(true,complete);
    if (!p.ok()) return p.win32_error==ERROR_FILE_NOT_FOUND && !c.ok() && c.win32_error==ERROR_FILE_NOT_FOUND;
    return !pending.empty() && c.ok() && pending==complete;
}
RecoveryResult recover_campaign(storage::Namespace& lease,const storage::Descriptor& descriptor,const RecoveryTransport& t) {
    RecoveryResult result;
    const auto refuse=[&](const char* why) { result.reason=why; return result; };
    if (descriptor.campaign.intent!=storage::CampaignIntent::recover || descriptor.campaign.recovery_basename.empty()) return refuse("recovery_explicit_archive_required");
    std::string old_journal;
    auto r=lease.recovery_record(false,old_journal);
    if(r.ok() || r.win32_error!=ERROR_FILE_NOT_FOUND) return refuse("recovery_previous_attempt_requires_review");
    if(!t.remote || !t.owner || !t.write || !t.read || !t.exists || !t.size || !t.count || !t.name) return refuse("recovery_provider_unavailable");
    const auto user=t.owner(t.remote);
    std::unique_ptr<storage::TransportArchive> archive;
    const auto& basename=descriptor.campaign.recovery_basename;
    if(!lease.reopen_transport(std::wstring(basename.begin(),basename.end()),archive).ok()) return refuse("recovery_archive_verification_failed");
    const auto& source=archive->metadata();
    const auto id=lease.metadata().namespace_id;
    const auto directory="ap-"+id.substr(0,40)+"/GAME-AUTOSAVE0";
    const auto contract="sentinel-campaign-v1\nnamespace="+id+"\ngeneration="+descriptor.identity.generation_fingerprint+
        "\nprovenance=synthetic-fixture\ncampaign=base\nstarting_stage=base_start\ndifficulty="+std::to_string(descriptor.campaign.difficulty)+"\nslot=AUTOSAVE0\n";
    std::string current_contract,current_record;
    if(!user || source.steam_user!=user || source.provider!=recovery_provider || source.quarantine) return refuse("recovery_native_user_or_provider_mismatch");
    if(descriptor.campaign.difficulty>3 || source.contract!=contract || source.directory!=directory ||
        !lease.campaign_record(false,current_contract).ok() || current_contract!=contract) return refuse("recovery_contract_or_options_mismatch");
    Checkpoint saved;
    if(!checkpoint(source.checkpoint,contract,directory,saved) || !matches(saved,source) ||
        !saved.files.count("game.details") || !saved.files.count("game_duration.dat") ||
        (saved.files.size()!=2 && saved.files.size()!=4)) return refuse("recovery_archive_checkpoint_mismatch");
    const auto marker="ap-"+id.substr(0,40)+"/sentinel-owner-"+id+".txt";
    const auto& identity=descriptor.identity;
    const auto ownership="sentinel-native-session-v1\nnamespace_id="+id+"\nseed_hex="+hex(identity.seed)+"\nteam="+std::to_string(*identity.team)+
        "\nslot="+std::to_string(*identity.slot)+"\ngeneration_fingerprint="+identity.generation_fingerprint+"\nprovenance=synthetic-fixture\n";
    const auto owned=[&] {
        std::string bytes(ownership.size(),'\0');
        return t.owner(t.remote)==user && t.size(t.remote,marker.c_str())==static_cast<int32_t>(bytes.size()) &&
            t.read(t.remote,marker.c_str(),bytes.data(),static_cast<int32_t>(bytes.size()))==static_cast<int32_t>(bytes.size()) && bytes==ownership;
    };
    if(!owned()) return refuse("recovery_native_marker_mismatch");
    const auto selection_key="ap-"+id.substr(0,40)+"/sentinel-selection-GAME.txt";
    std::string previous_selection;
    const bool had_selection=t.exists(t.remote,selection_key.c_str());
    if(had_selection) {
        const auto length=t.size(t.remote,selection_key.c_str());
        if(length<=0 || length>2048) return refuse("recovery_selection_unavailable");
        previous_selection.resize(static_cast<size_t>(length));
        if(t.read(t.remote,selection_key.c_str(),previous_selection.data(),length)!=length) return refuse("recovery_selection_unavailable");
    }
    storage::TransportMetadata current=source; Payloads before;
    if(!capture(t,directory,current,before)) return refuse("recovery_target_inventory_unavailable_or_unknown");
    r=lease.campaign_record(true,current_record);
    if(!r.ok() && r.win32_error!=ERROR_FILE_NOT_FOUND) return refuse("recovery_target_metadata_unavailable");
    Checkpoint target;
    const bool recorded=checkpoint(current_record,contract,directory,target);
    // A normal retail write owns the primary pair. Native rotation can leave
    // a separate BACKUP pair which that operation/continuity never claimed.
    // Keep these opaque bytes: quarantine and recheck the FULL inventory, but
    // classify/restore only the operation's files. Never infer backup validity.
    auto tracked=current, expected_after=source;
    if(source.files.size()==2 && recorded && target.files.size()==2 &&
        target.files.count("game.details") && target.files.count("game_duration.dat")) {
        unsigned auxiliaries=0;
        for(const auto& f:current.files) if(f.name=="game.details-BACKUP" || f.name=="game_duration.dat-BACKUP") {
            ++auxiliaries; expected_after.files.push_back(f);
        }
        if(auxiliaries==1) return refuse("recovery_auxiliary_pair_incomplete");
        if(auxiliaries==2) {
            tracked.files.erase(std::remove_if(tracked.files.begin(),tracked.files.end(),[](const auto& f){
                return f.name=="game.details-BACKUP" || f.name=="game_duration.dat-BACKUP";}),tracked.files.end());
            result.preserved_auxiliaries=2;
        }
    }
    const bool valid=recorded && matches(target,tracked);
    const bool same=equal(source,tracked);
    if(recorded && target.number>saved.number) { result.state=RecoveryState::newer; return refuse("recovery_newer_recorded_progress"); }
    if(valid && same && current_record==source.checkpoint) {
        result.state=RecoveryState::same; result.complete=true; result.reason="recovery_already_exact"; return result;
    }
    if(valid && target.number==saved.number) { result.state=RecoveryState::conflict; return refuse("recovery_valid_ordering_conflict"); }
    result.state=valid?RecoveryState::older:tracked.files.size()<source.files.size()?RecoveryState::missing_incomplete:RecoveryState::corrupt_unknown;
    if(!valid && !tracked.files.empty()) {
        bool known_subset=recorded && tracked.files.size()<target.files.size();
        for(const auto& f:tracked.files) {
            const auto expected=target.files.find(f.name);
            known_subset=known_subset && expected!=target.files.end() && expected->second==std::make_pair(f.size,hash(f));
        }
        if(!known_subset) { result.state=RecoveryState::corrupt_unknown; return refuse("recovery_unknown_requires_conflict_decision"); }
    }
    // No deletion API: unknown/extra files are never swept away. A complete
    // source must cover every tracked target file before any write is possible.
    // Any untracked native auxiliary pair remains byte-identical in expected.
    for(const auto& f:tracked.files) if(std::none_of(source.files.begin(),source.files.end(),[&](const auto& s){return s.name==f.name;})) return refuse("recovery_target_extra_payload_conflict");
    Payloads replacement;
    for(size_t i=0;i<source.files.size();++i) {
        std::string bytes(source.files[i].size,'\0');
        if(!archive->read(i,0,bytes.data(),source.files[i].size).ok()) return refuse("recovery_staging_failed");
        replacement.bytes.push_back(std::move(bytes));
    }
    current.checkpoint=current_record;
    if(!current.files.empty()) {
        storage::Backup quarantine;
        auto evidence=current; evidence.quarantine=true; evidence.operation_id=0; evidence.process_id=GetCurrentProcessId();
        FILETIME created{},exit{},kernel{},cpu{};
        if(!GetProcessTimes(GetCurrentProcess(),&created,&exit,&kernel,&cpu)) return refuse("recovery_quarantine_process_identity_failed");
        evidence.process_created=(uint64_t(created.dwHighDateTime)<<32)|created.dwLowDateTime;
        if(!lease.backup_transport(evidence,Payloads::read,&before,quarantine).ok()) return refuse("recovery_quarantine_failed");
        result.quarantine=quarantine.path;
        const auto leaf=result.quarantine.substr(result.quarantine.find_last_of(L"\\/")+1);
        std::unique_ptr<storage::TransportArchive> verified;
        if(!lease.reopen_transport(leaf,verified).ok() || !equal(verified->metadata(),current)) return refuse("recovery_quarantine_verification_failed");
    }
    const auto leaf=result.quarantine.substr(result.quarantine.find_last_of(L"\\/")+1);
    std::string quarantine_basename; for(const auto c:leaf) quarantine_basename+=static_cast<char>(c);
    const auto journal="sentinel-recovery-v1\nnamespace="+id+"\nbackup="+basename+"\nquarantine="+quarantine_basename+
        "\nprevious_checkpoint_hex="+hex(current_record)+"\nprevious_selection_present="+(had_selection?"1":"0")+
        "\nprevious_selection_hex="+hex(previous_selection)+"\n";
    // Re-observe after staging/quarantine, still before the durable write-ahead
    // barrier. No native campaign job exists at this qualified startup boundary.
    storage::TransportMetadata check=source; Payloads check_bytes;
    std::string check_record;
    const auto checked=lease.campaign_record(true,check_record);
    std::string selection_before(previous_selection.size(),'\0');
    if(!owned() || !capture(t,directory,check,check_bytes) || !equal(check,current) ||
        (checked.ok()!=r.ok()) || check_record!=current_record || t.exists(t.remote,selection_key.c_str())!=had_selection ||
        (had_selection && (t.size(t.remote,selection_key.c_str())!=static_cast<int32_t>(previous_selection.size()) ||
            t.read(t.remote,selection_key.c_str(),selection_before.data(),static_cast<int32_t>(selection_before.size()))!=static_cast<int32_t>(selection_before.size()) ||
            selection_before!=previous_selection))) return refuse("recovery_concurrent_target_change");
    if(!lease.publish_recovery_record(false,journal).ok()) return refuse("recovery_journal_failed");
    for(size_t i=0;i<source.files.size();++i) {
        if(!owned()) return refuse("recovery_provider_changed_during_transaction");
        const auto key=directory+"/"+source.files[i].name;
        result.mutated=true;
        if(!t.write(t.remote,key.c_str(),replacement.bytes[i].data(),static_cast<int32_t>(replacement.bytes[i].size()))) return refuse("recovery_native_write_failed");
    }
    if(!owned() || !capture(t,directory,check,check_bytes) || !equal(expected_after,check)) return refuse("recovery_target_verification_failed");
    const auto selection="sentinel-native-selection-v1\nnamespace_id="+id+"\ncampaign=GAME-\nname=AUTOSAVE0\n";
    if(!t.write(t.remote,selection_key.c_str(),selection.data(),static_cast<int32_t>(selection.size()))) return refuse("recovery_selection_failed");
    std::string selection_check(selection.size(),'\0');
    if(t.size(t.remote,selection_key.c_str())!=static_cast<int32_t>(selection.size()) ||
        t.read(t.remote,selection_key.c_str(),selection_check.data(),static_cast<int32_t>(selection.size()))!=static_cast<int32_t>(selection.size()) ||
        selection_check!=selection || !owned()) return refuse("recovery_selection_verification_failed");
    if(!lease.publish_campaign_record(true,source.checkpoint,!r.ok()).ok()) return refuse("recovery_continuity_publication_failed");
    if(!lease.publish_recovery_record(true,journal).ok()) return refuse("recovery_completion_publication_failed");
    result.complete=true; result.reason="recovery_native_payload_verified"; return result;
}
bool Session::recover_startup(const RecoveryTransport& transport) {
    if(!recovery_requested()) return true;
    if(!acquire_native_root_lock() || routed() || !lease_) {
        btrace.record(BStage::resume,BStatus::refused,"recovery_exclusive_target_unavailable",0,
            {{"classification",RecoveryState::unavailable},{"mutated",false}});
        fail(SessionFault::native_campaign); return false;
    }
    const auto result=recover_campaign(*lease_,recovery_descriptor_,transport);
    if(result.complete) {
        auto descriptor=recovery_descriptor_; descriptor.campaign.intent=storage::CampaignIntent::resume;
        if(!campaign_run.configure(*this,descriptor,*lease_)) return false;
    }
    btrace.record(BStage::resume,result.complete?BStatus::succeeded:BStatus::refused,result.reason,0,
        {{"classification",result.state},{"mutated",result.mutated},{"complete",result.complete},{"quarantined",!result.quarantine.empty()},
         {"preserved_auxiliaries",result.preserved_auxiliaries}});
    if(!result.complete) { fail(SessionFault::native_campaign); return false; }
    return true;
}
}
