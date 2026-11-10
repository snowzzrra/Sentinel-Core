// Copyright (c) 2026 snowzzrra. MIT; see ../LICENSE.
struct NativeDisk {
    fs::path path;
    std::vector<std::string> names;
    static uint64_t owner(uintptr_t) { return 76561198000000001ull; }
    static bool put(uintptr_t self,const char* key,const void* bytes,int32_t size) {
        const auto path=reinterpret_cast<NativeDisk*>(self)->path/fs::path(key);
        fs::create_directories(path.parent_path());
        const auto file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if (file==INVALID_HANDLE_VALUE) return false;
        DWORD written=0;
        const bool ok=WriteFile(file,bytes,static_cast<DWORD>(size),&written,nullptr) && written==static_cast<DWORD>(size) && FlushFileBuffers(file);
        CHECK(CloseHandle(file)); return ok;
    }
    static int32_t get(uintptr_t self,const char* key,void* bytes,int32_t size) {
        std::ifstream file(reinterpret_cast<NativeDisk*>(self)->path/fs::path(key),std::ios::binary);
        if (!file) return -1;
        file.read(static_cast<char*>(bytes),size); return static_cast<int32_t>(file.gcount());
    }
    static bool exists(uintptr_t self,const char* key) { return fs::is_regular_file(reinterpret_cast<NativeDisk*>(self)->path/fs::path(key)); }
    static int32_t size(uintptr_t self,const char* key) {
        const auto path=reinterpret_cast<NativeDisk*>(self)->path/fs::path(key);
        return fs::is_regular_file(path)?static_cast<int32_t>(fs::file_size(path)):-1;
    }
    static int32_t count(uintptr_t self) {
        auto& disk=*reinterpret_cast<NativeDisk*>(self); disk.names.clear();
        for (const auto& file:fs::recursive_directory_iterator(disk.path)) if (file.is_regular_file())
            disk.names.push_back(fs::relative(file.path(),disk.path).generic_string());
        return static_cast<int32_t>(disk.names.size());
    }
    static const char* name(uintptr_t self,int32_t index,int32_t* size) {
        auto& disk=*reinterpret_cast<NativeDisk*>(self); const auto& key=disk.names.at(index);
        *size=NativeDisk::size(self,key.c_str()); return key.c_str();
    }
    sentinel::save::RecoveryTransport transport() {
        return {reinterpret_cast<uintptr_t>(this),owner,put,get,exists,size,count,name};
    }
    void write(const std::string& name,const std::string& bytes) { CHECK(put(reinterpret_cast<uintptr_t>(this),name.c_str(),bytes.data(),static_cast<int32_t>(bytes.size()))); }
};
std::string native_checkpoint(const Namespace& owner,uint64_t number,const std::vector<std::string>& bytes) {
    auto value=metadata_descriptor(L"");
    auto result=sentinel::save::campaign_contract(value,owner.metadata().namespace_id)+"checkpoint="+std::to_string(number)+
        "\nmap=game/hub/hub\nsubtype=1\nfiles=2\n";
    const std::string names[]{"game.details","game_duration.dat"};
    for (size_t i=0;i<2;++i) result+="file="+owner.metadata().native_root+"/GAME-AUTOSAVE0/"+names[i]+","+
        std::to_string(bytes[i].size())+","+sha256(bytes[i])+"\n";
    return result+"state=native_saved_readback_verified\n";
}
TransportMetadata protocol_metadata(Namespace& owner,TransportSource& source,uint64_t number,bool automatic=true) {
    auto metadata=transport_metadata(owner,source);
    metadata.directory=owner.metadata().native_root+"/GAME-AUTOSAVE0";
    metadata.files[1].name="game_duration.dat";
    metadata.steam_user=NativeDisk::owner(0); metadata.provider=sentinel::save::recovery_provider;
    metadata.contract=sentinel::save::campaign_contract(metadata_descriptor(L""),owner.metadata().namespace_id);
    metadata.checkpoint=native_checkpoint(owner,number,source.bytes); metadata.automatic=automatic;
    return metadata;
}
const std::vector<std::string> confirmed_bytes{"confirmed details: independent generation","confirmed duration: independent generation"};
void seed_protocol(const std::wstring& root,bool legacy=false) {
    CHECK(CreateDirectoryW(root.c_str(),nullptr));
    std::unique_ptr<Namespace> lease; const auto value=metadata_descriptor(root); CHECK(prepare(value,lease).ok());
    NativeDisk disk{fs::path(root)/"provider",{}}; fs::create_directory(disk.path);
    const auto native=lease->metadata().native_root;
    const auto identity="sentinel-native-session-v1\nnamespace_id="+lease->metadata().namespace_id+
        "\nseed_hex="+hex_bytes(value.identity.seed)+"\nteam=0\nslot=1\ngeneration_fingerprint="+value.identity.generation_fingerprint+"\nprovenance=synthetic-fixture\n";
    disk.write(native+"/sentinel-owner-"+lease->metadata().namespace_id+".txt",identity);
    disk.write(native+"/GAME-AUTOSAVE0/game.details",confirmed_bytes[0]);
    disk.write(native+"/GAME-AUTOSAVE0/game_duration.dat",confirmed_bytes[1]);
    const auto contract=sentinel::save::campaign_contract(value,lease->metadata().namespace_id);
    CHECK(lease->publish_campaign_record(false,contract,true).ok());
    if (legacy) { CHECK(lease->publish_campaign_record(true,native_checkpoint(*lease,7,confirmed_bytes),true).ok()); return; }
    CheckpointWrite transaction; CHECK(lease->begin_checkpoint(value,NativeDisk::owner(0),transaction).ok());
    Backup copy; TransportSource source; source.attempt=&copy; source.bytes=confirmed_bytes;
    const auto metadata=protocol_metadata(*lease,source,1);
    CHECK(lease->backup_transport(metadata,TransportSource::copy,&source,copy).ok());
    CHECK(lease->stage_checkpoint(transaction,fs::path(copy.path).filename().wstring()).ok());
    CHECK(lease->receipt_checkpoint(transaction).ok()); CHECK(lease->commit_checkpoint(transaction).ok());
}
bool protocol_select(Namespace& lease, NativeDisk& disk, const std::wstring& phase,
                     const std::function<void()>& inject) {
    if (phase == L"selection") inject();
    const auto selection = "sentinel-native-selection-v1\nnamespace_id=" + lease.metadata().namespace_id +
                           "\ncampaign=GAME-\nname=AUTOSAVE0\n";
    if (!NativeDisk::put(reinterpret_cast<uintptr_t>(&disk),
                         (lease.metadata().native_root + "/sentinel-selection-GAME.txt").c_str(), selection.data(),
                         static_cast<int32_t>(selection.size())))
        return false;
    return true;
}
bool protocol_native_payloads(NativeDisk& disk, const TransportMetadata& metadata, const TransportSource& source) {
    for (size_t i = 0; i < 2; ++i)
        if (!NativeDisk::put(reinterpret_cast<uintptr_t>(&disk),
                             (metadata.directory + "/" + metadata.files[i].name).c_str(), source.bytes[i].data(),
                             static_cast<int32_t>(source.bytes[i].size())))
            return false;
    return true;
}
bool protocol_save(Namespace& lease,NativeDisk& disk,const std::wstring& phase,const std::function<void()>& inject) {
    CheckpointWrite transaction; const auto value=metadata_descriptor(disk.path.parent_path().wstring());
    if (phase==L"begin") inject();
    if (!lease.begin_checkpoint(value,NativeDisk::owner(0),transaction).ok()) return false;
    Backup copy; TransportSource source; source.attempt=&copy;
    source.bytes={"new details generation "+std::to_string(transaction.number),"new duration generation "+std::to_string(transaction.number)};
    auto metadata=protocol_metadata(lease,source,transaction.number);
    if (phase==L"native") inject();
    if (!protocol_native_payloads(disk, metadata, source)) return false;
    if (phase==L"archive") inject();
    if (!lease.backup_transport(metadata,TransportSource::copy,&source,copy).ok()) return false;
    if (phase==L"stage") inject();
    if (!lease.stage_checkpoint(transaction,fs::path(copy.path).filename().wstring()).ok()) return false;
    if (phase==L"receipt") inject();
    if (!lease.receipt_checkpoint(transaction).ok()) return false;
    if (!protocol_select(lease, disk, phase, inject)) return false;
    if (phase==L"commit") inject();
    if (!lease.commit_checkpoint(transaction).ok()) return false;
    if (phase==L"cleanup") inject();
    lease.prune_checkpoints(); return true;
}
int protocol_child(const wchar_t* root,const wchar_t* phase,const wchar_t* mode,const wchar_t* suffix,const wchar_t* stem) {
    using namespace sentinel::testing;
    const auto selected=static_cast<FileFault>(std::wcstoul(mode,nullptr,10));
    HANDLE entered=OpenEventW(EVENT_MODIFY_STATE,FALSE,(std::wstring(stem)+L"-entered").c_str());
    HANDLE release=OpenEventW(SYNCHRONIZE,FALSE,(std::wstring(stem)+L"-release").c_str()); CHECK(entered && release);
    std::unique_ptr<Namespace> lease; CHECK(reopen(metadata_descriptor(root),lease).ok());
    NativeDisk disk{fs::path(root)/"provider",{}};
    const auto inject=[&] { file_fault(selected,suffix,entered,release); };
    if (std::wstring_view(phase)==L"restart") {
        inject(); lease->publish_restart_record(false,"replacement validated record\n");
    } else if (std::wstring_view(phase)==L"migration" || std::wstring_view(phase)==L"recovery") {
        inject(); sentinel::save::recover_checkpoint(*lease,metadata_descriptor(root),disk.transport());
    } else {
        if (std::wstring_view(phase)==L"cleanup") CHECK(protocol_save(*lease,disk,L"",[] {}));
        protocol_save(*lease,disk,phase,inject);
    }
    CHECK(file_fault_hits()==1); file_fault(FileFault::none,L"");
    CHECK(CloseHandle(entered) && CloseHandle(release)); return 0;
}
void interrupt_protocol(const std::wstring& root,const std::wstring& phase,sentinel::testing::FileFault mode,const std::wstring& suffix) {
    const auto stem=L"Local\\Sentinel-protocol-"+std::to_wstring(GetCurrentProcessId());
    HANDLE entered=CreateEventW(nullptr,TRUE,FALSE,(stem+L"-entered").c_str());
    HANDLE release=CreateEventW(nullptr,TRUE,FALSE,(stem+L"-release").c_str()); CHECK(entered && release);
    wchar_t executable[MAX_PATH]{}; CHECK(GetModuleFileNameW(nullptr,executable,MAX_PATH));
    auto command=L"\""+std::wstring(executable)+L"\" --protocol-child \""+root+L"\" "+phase+L" "+std::to_wstring(static_cast<unsigned>(mode))+
        L" \""+suffix+L"\" \""+stem+L"\"";
    STARTUPINFOW startup{}; startup.cb=sizeof(startup); PROCESS_INFORMATION process{};
    CHECK(CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process));
    const HANDLE waits[]{entered,process.hProcess};
    const bool terminate=mode==sentinel::testing::FileFault::after_write || mode==sentinel::testing::FileFault::after_truncate ||
        mode==sentinel::testing::FileFault::after_flush || mode==sentinel::testing::FileFault::after_erase;
    const auto waited=WaitForMultipleObjects(2,waits,FALSE,15000);
    if (waited!=(terminate?WAIT_OBJECT_0:WAIT_OBJECT_0+1)) std::fprintf(stderr,"protocol wait phase=%ls suffix=%ls mode=%u wait=%lu\n",phase.c_str(),suffix.c_str(),static_cast<unsigned>(mode),waited);
    CHECK(waited==(terminate?WAIT_OBJECT_0:WAIT_OBJECT_0+1));
    if (terminate) CHECK(TerminateProcess(process.hProcess,sentinel::testing::interrupted_exit));
    CHECK(WaitForSingleObject(process.hProcess,4000)==WAIT_OBJECT_0);
    DWORD code=0; CHECK(GetExitCodeProcess(process.hProcess,&code));
    if (code!=(terminate?sentinel::testing::interrupted_exit:0)) std::fprintf(stderr,"protocol child phase=%ls suffix=%ls mode=%u exit=%lu\n",phase.c_str(),suffix.c_str(),static_cast<unsigned>(mode),code);
    CHECK(code==(terminate?sentinel::testing::interrupted_exit:0));
    CHECK(CloseHandle(process.hThread) && CloseHandle(process.hProcess) && CloseHandle(entered) && CloseHandle(release));
}
void verify_archive(Namespace& lease,const CheckpointPlan& plan,const std::vector<std::string>& bytes) {
    std::unique_ptr<TransportArchive> archive;
    CHECK(lease.reopen_transport(std::wstring(plan.archive.begin(),plan.archive.end()),archive).ok());
    CHECK(archive->metadata().files.size()==bytes.size());
    for (size_t i=0;i<bytes.size();++i) {
        std::string actual(bytes[i].size(),'\0'); CHECK(archive->read(i,0,actual.data(),static_cast<uint32_t>(actual.size())).ok()); CHECK(actual==bytes[i]);
    }
}
int previous_checkpoint(Fixture& fixture) {
    const auto root=fixture.path+L"\\previous"; seed_protocol(root);
    interrupt_protocol(root,L"native",sentinel::testing::FileFault::after_write,L"\\game.details");
    std::unique_ptr<Namespace> lease; auto value=metadata_descriptor(root); CHECK(reopen(value,lease).ok());
    CheckpointPlan plan; CHECK(lease->checkpoint_plan(value,plan).ok() && plan.checkpoint==1 && plan.interrupted_write);
    verify_archive(*lease,plan,confirmed_bytes);
    NativeDisk disk{fs::path(root)/"provider",{}};
    const auto recovered=sentinel::save::recover_checkpoint(*lease,value,disk.transport()); CHECK(recovered.complete && recovered.mutated);
    CHECK(lease->checkpoint_plan(value,plan).ok() && plan.state==CheckpointState::confirmed && plan.checkpoint==1);
    CHECK(read((disk.path/fs::path(lease->metadata().native_root+"/GAME-AUTOSAVE0/game.details")).wstring())==confirmed_bytes[0]);
    verify_archive(*lease,plan,confirmed_bytes);
    lease.reset(); CHECK(can_resume(root));
    std::puts("PASS previous_checkpoint_generation and exact bytes after crash, recovery and resume admission"); return 0;
}
void protocol_write_faults(Fixture& fixture, unsigned& attempts,
                           const std::vector<std::pair<std::wstring, std::wstring>>& windows) {
    using sentinel::testing::FileFault;
    for (const auto& window : windows)
        for (const auto mode :
             {FileFault::short_write, FileFault::flush, FileFault::after_write, FileFault::after_flush}) {
            const auto root = fixture.path + L"\\protocol-" + std::to_wstring(++attempts);
            const bool migration = window.first == L"migration";
            seed_protocol(root, migration);
            if (window.first == L"recovery") {
                std::unique_ptr<Namespace> lease;
                CHECK(reopen(metadata_descriptor(root), lease).ok());
                CheckpointWrite write;
                CHECK(lease->begin_checkpoint(metadata_descriptor(root), NativeDisk::owner(0), write).ok());
                NativeDisk disk{fs::path(root) / "provider", {}};
                disk.write(lease->metadata().native_root + "/GAME-AUTOSAVE0/game.details", "interrupted native bytes");
            }
            interrupt_protocol(root, window.first, mode, window.second);
            std::unique_ptr<Namespace> lease;
            const auto value = metadata_descriptor(root);
            CHECK(reopen(value, lease).ok());
            NativeDisk disk{fs::path(root) / "provider", {}};
            const auto recovered = sentinel::save::recover_checkpoint(*lease, value, disk.transport());
            if (!recovered.complete)
                std::fprintf(stderr, "recovery phase=%ls suffix=%ls mode=%u reason=%s\n", window.first.c_str(),
                             window.second.c_str(), static_cast<unsigned>(mode), recovered.reason);
            CHECK(recovered.complete);
            CheckpointPlan plan;
            CHECK(lease->checkpoint_plan(value, plan).ok() && plan.state == CheckpointState::confirmed);
            if (migration) {
                CHECK(plan.checkpoint == 7);
                verify_archive(*lease, plan, confirmed_bytes);
            } else if (plan.checkpoint == 1)
                verify_archive(*lease, plan, confirmed_bytes);
            else
                CHECK(plan.checkpoint == 2);
            const auto again = sentinel::save::recover_checkpoint(*lease, value, disk.transport());
            CHECK(again.complete && !again.mutated);
        }
}
bool truncatable_window(const std::pair<std::wstring, std::wstring>& window) {
    if (window.first == L"native" || window.first == L"archive" || window.first == L"selection" ||
        window.second == L"\\payload-0.bin" || window.second == L"\\transport.manifest" ||
        window.second == L"\\campaign.checkpoint.legacy" || window.second == L"\\payload-1.bin" ||
        window.second == L"\\automatic-*" ||
        (window.first == L"recovery" && window.second != L"\\recovery.pending" &&
         window.second != L"\\recovery.complete"))
        return false;
    return true;
}
void protocol_truncate_faults(Fixture& fixture, unsigned& attempts,
                              const std::vector<std::pair<std::wstring, std::wstring>>& windows) {
    using sentinel::testing::FileFault;
    for (const auto mode : {FileFault::truncate, FileFault::after_truncate})
        for (const auto& window : windows) {
            if (!truncatable_window(window)) continue;
            const auto root = fixture.path + L"\\truncate-" + std::to_wstring(++attempts);
            seed_protocol(root, window.first == L"migration");
            if (window.first == L"recovery") {
                std::unique_ptr<Namespace> lease;
                CHECK(reopen(metadata_descriptor(root), lease).ok());
                CheckpointWrite write;
                CHECK(lease->begin_checkpoint(metadata_descriptor(root), NativeDisk::owner(0), write).ok());
            }
            interrupt_protocol(root, window.first, mode, window.second);
            std::unique_ptr<Namespace> lease;
            CHECK(reopen(metadata_descriptor(root), lease).ok());
            NativeDisk disk{fs::path(root) / "provider", {}};
            const auto recovered =
                sentinel::save::recover_checkpoint(*lease, metadata_descriptor(root), disk.transport());
            if (!recovered.complete)
                std::fprintf(stderr, "truncate recovery phase=%ls reason=%s\n", window.first.c_str(), recovered.reason);
            CHECK(recovered.complete);
        }
}
void protocol_cleanup_faults(Fixture& fixture, unsigned& attempts) {
    using sentinel::testing::FileFault;
    for (const auto mode : {FileFault::erase, FileFault::after_erase})
        for (const auto* suffix : {L"\\payload-0.bin", L"\\payload-1.bin", L"\\transport.manifest",
                                   L"\\transport-backup-*", L"\\automatic-*"}) {
            const auto root = fixture.path + L"\\cleanup-" + std::to_wstring(++attempts);
            seed_protocol(root);
            interrupt_protocol(root, L"cleanup", mode, suffix);
            std::unique_ptr<Namespace> lease;
            const auto value = metadata_descriptor(root);
            CHECK(reopen(value, lease).ok());
            CheckpointPlan plan;
            CHECK(lease->checkpoint_plan(value, plan).ok() && plan.state == CheckpointState::confirmed &&
                  plan.checkpoint == 3);
            CHECK(lease->prune_checkpoints().ok());
            unsigned automatic = 0;
            for (const auto& entry : fs::directory_iterator(lease->metadata().path))
                automatic += entry.path().filename().wstring().find(L"automatic-") == 0;
            CHECK(automatic == 2);
        }
}
void protocol_discard_faults(Fixture& fixture, unsigned& attempts) {
    using sentinel::testing::FileFault;
    for (const auto mode : {FileFault::erase, FileFault::after_erase})
        for (const auto* suffix : {L"\\checkpoint-B.pending", L"\\checkpoint-B.receipt", L"\\checkpoint-B.commit",
                                   L"\\checkpoint-B.started"}) {
            const auto root = fixture.path + L"\\discard-" + std::to_wstring(++attempts);
            seed_protocol(root);
            {
                std::unique_ptr<Namespace> lease;
                const auto value = metadata_descriptor(root);
                CHECK(reopen(value, lease).ok());
                NativeDisk disk{fs::path(root) / "provider", {}};
                CHECK(protocol_save(*lease, disk, L"", [] {}));
                CheckpointPlan plan;
                CHECK(lease->checkpoint_plan(value, plan).ok());
                write((fs::path(root) / plan.archive / "payload-0.bin").wstring(), "damaged candidate archive");
            }
            interrupt_protocol(root, L"recovery", mode, suffix);
            std::unique_ptr<Namespace> lease;
            const auto value = metadata_descriptor(root);
            CHECK(reopen(value, lease).ok());
            NativeDisk disk{fs::path(root) / "provider", {}};
            CHECK(sentinel::save::recover_checkpoint(*lease, value, disk.transport()).complete);
            CheckpointPlan plan;
            CHECK(lease->checkpoint_plan(value, plan).ok() && plan.state == CheckpointState::confirmed &&
                  plan.checkpoint == 1);
            verify_archive(*lease, plan, confirmed_bytes);
        }
}
void protocol_unproven_payloads(Fixture& fixture) {
    using sentinel::testing::FileFault;

    const auto root = fixture.path + L"\\unproven";
    seed_protocol(root);
    std::unique_ptr<Namespace> lease;
    const auto value = metadata_descriptor(root);
    CHECK(reopen(value, lease).ok());
    NativeDisk disk{fs::path(root) / "provider", {}};
    disk.write(lease->metadata().native_root + "/GAME-AUTOSAVE0/game.details", "external bytes without an intent");
    const auto refused = sentinel::save::recover_checkpoint(*lease, value, disk.transport());
    CHECK(!refused.complete && !refused.mutated &&
          std::string_view(refused.reason) == "recovery_external_payload_change" && !refused.quarantine.empty());
    CHECK(lease->discard_checkpoint('A').ok());
    const auto first = sentinel::save::recover_checkpoint(*lease, value, disk.transport());
    CHECK(!first.complete && !first.mutated && std::string_view(first.reason) == "recovery_no_confirmed_predecessor" &&
          !first.quarantine.empty());
}
void protocol_retention(Fixture& fixture) {
    using sentinel::testing::FileFault;

    const auto root = fixture.path + L"\\retention";
    seed_protocol(root);
    std::unique_ptr<Namespace> lease;
    const auto value = metadata_descriptor(root);
    CHECK(reopen(value, lease).ok());
    NativeDisk disk{fs::path(root) / "provider", {}};
    Backup manual;
    TransportSource source;
    source.attempt = &manual;
    source.bytes = confirmed_bytes;
    CHECK(lease->backup_transport(protocol_metadata(*lease, source, 1, false), TransportSource::copy, &source, manual)
              .ok());
    const auto manual_bytes = read(manual.path + L"\\payload-0.bin");
    for (unsigned i = 0; i < 4; ++i) CHECK(protocol_save(*lease, disk, L"", [] {}));
    CHECK(read(manual.path + L"\\payload-0.bin") == manual_bytes);
    CheckpointPlan plan;
    CHECK(lease->checkpoint_plan(value, plan).ok() && plan.checkpoint == 5 && plan.previous_side);
    auto foreign = value;
    foreign.campaign.difficulty = 2;
    const auto refused = sentinel::save::recover_checkpoint(*lease, foreign, disk.transport());
    CHECK(!refused.complete && !refused.mutated);
    unsigned automatic = 0;
    for (const auto& entry : fs::directory_iterator(lease->metadata().path))
        automatic += entry.path().filename().wstring().find(L"automatic-") == 0;
    CHECK(automatic == 2);
}
void protocol_future_format(Fixture& fixture) {
    using sentinel::testing::FileFault;

    const auto root = fixture.path + L"\\future-format";
    seed_protocol(root, true);
    std::unique_ptr<Namespace> lease;
    const auto value = metadata_descriptor(root);
    CHECK(reopen(value, lease).ok());
    NativeDisk disk{fs::path(root) / "provider", {}};
    CHECK(sentinel::save::recover_checkpoint(*lease, value, disk.transport()).complete);
    CHECK(lease->publish_campaign_record(true, "sentinel-checkpoint-ab-v2\n", false).ok());
    CheckpointPlan plan;
    CHECK(lease->checkpoint_plan(value, plan).ok() && plan.state == CheckpointState::unsupported);
    const auto refused = sentinel::save::recover_checkpoint(*lease, value, disk.transport());
    CHECK(!refused.complete && !refused.mutated);
}
void protocol_auxiliary_change(Fixture& fixture, unsigned& attempts) {
    using sentinel::testing::FileFault;

    const auto root = fixture.path + L"\\auxiliary-change";
    seed_protocol(root);
    ++attempts;
    NativeDisk disk{fs::path(root) / "provider", {}};
    const auto value = metadata_descriptor(root);
    std::string directory;
    {
        std::unique_ptr<Namespace> lease;
        CHECK(reopen(value, lease).ok());
        directory = lease->metadata().native_root + "/GAME-AUTOSAVE0";
        disk.write(directory + "/game.details-BACKUP", "opaque details");
        disk.write(directory + "/game_duration.dat-BACKUP", "opaque duration");
        CheckpointWrite transaction;
        CHECK(lease->begin_checkpoint(value, NativeDisk::owner(0), transaction).ok());
        disk.write(directory + "/game.details", "interrupted bytes");
    }
    interrupt_protocol(root, L"recovery", FileFault::after_write, L"\\game.details");
    disk.write(directory + "/game.details-BACKUP", "opaque detail");
    std::unique_ptr<Namespace> lease;
    CHECK(reopen(value, lease).ok());
    const auto refused = sentinel::save::recover_checkpoint(*lease, value, disk.transport());
    CHECK(!refused.complete && !refused.mutated &&
          std::string_view(refused.reason) == "recovery_concurrent_target_change");
    disk.write(directory + "/game.details-BACKUP", "opaque details");
    CHECK(sentinel::save::recover_checkpoint(*lease, value, disk.transport()).complete);
}
void protocol_first_receipt(Fixture& fixture) {
    const auto root=fixture.path+L"\\first-receipt"; seed_protocol(root);
    std::unique_ptr<Namespace> lease; const auto value=metadata_descriptor(root); CHECK(reopen(value,lease).ok());
    CHECK(DeleteFileW((lease->metadata().path+L"\\checkpoint-A.commit").c_str()));
    NativeDisk disk{fs::path(root)/"provider",{}};
    const auto recovered=sentinel::save::recover_checkpoint(*lease,value,disk.transport());
    CHECK(recovered.complete && !recovered.mutated && std::string_view(recovered.reason)=="interrupted_save_recovered");
    CheckpointPlan plan;
    CHECK(lease->checkpoint_plan(value,plan).ok() && plan.state==CheckpointState::confirmed && plan.checkpoint==1);
}
void protocol_archive_unavailable(Fixture& fixture) {
    const auto root=fixture.path+L"\\archive-busy"; seed_protocol(root);
    std::unique_ptr<Namespace> lease; const auto value=metadata_descriptor(root); CHECK(reopen(value,lease).ok());
    NativeDisk disk{fs::path(root)/"provider",{}};
    CHECK(protocol_save(*lease,disk,L"",[] {}));
    CheckpointPlan plan; CHECK(lease->checkpoint_plan(value,plan).ok() && plan.checkpoint==2);
    const auto archive=fs::path(root)/plan.archive;
    const auto pending=lease->metadata().path+L"\\checkpoint-B.pending";
    const auto committed=read(lease->metadata().path+L"\\checkpoint-B.commit");
    HANDLE busy=CreateFileW((archive/"payload-0.bin").c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);
    CHECK(busy!=INVALID_HANDLE_VALUE);
    const auto unavailable=lease->checkpoint_plan(value,plan);
    CHECK(!unavailable.ok() && unavailable.win32_error==ERROR_SHARING_VIOLATION);
    const auto refused=sentinel::save::recover_checkpoint(*lease,value,disk.transport());
    CHECK(!refused.complete && !refused.mutated && fs::exists(pending) && fs::exists(archive));
    CHECK(read(lease->metadata().path+L"\\checkpoint-B.commit")==committed);
    CHECK(CloseHandle(busy));
    using namespace sentinel::testing;
    file_fault(FileFault::timeout,L"\\payload-0.bin");
    const auto timed_out=lease->checkpoint_plan(value,plan);
    CHECK(!timed_out.ok() && timed_out.win32_error==ERROR_TIMEOUT);
    const auto unknown=sentinel::save::recover_checkpoint(*lease,value,disk.transport());
    CHECK(!unknown.complete && !unknown.mutated && fs::exists(pending) && fs::exists(archive));
    file_fault(FileFault::none,L"");
    write((archive/"unrelated.txt").wstring(),"unrelated");
    CHECK(!lease->checkpoint_plan(value,plan).ok());
    CHECK(!sentinel::save::recover_checkpoint(*lease,value,disk.transport()).mutated);
    CHECK(fs::exists(pending) && fs::exists(archive));
    CHECK(DeleteFileW((archive/"unrelated.txt").c_str()));
    CHECK(lease->checkpoint_plan(value,plan).ok() && plan.state==CheckpointState::confirmed && plan.checkpoint==2);
}
void protocol_record_replacement(Namespace& lease,bool restart) {
    using namespace sentinel::testing;
    const auto publish=[&](const std::string& text) { return restart ? lease.publish_restart_record(false,text) : lease.publish_recovery_record(false,text,true); };
    const auto load=[&](std::string& text) { return restart ? lease.restart_record(false,text) : lease.recovery_record(false,text); };
    const auto name=restart?L"\\stage.restart.pending":L"\\recovery.pending";
    CHECK(publish("previous validated record\n").ok());
    for (auto fault : {FileFault::short_write,FileFault::truncate,FileFault::flush}) {
        file_fault(fault,name);
        CHECK(!publish("replacement validated record\n").ok() && file_fault_hits()==1);
        file_fault(FileFault::none,L"");
        std::string text; CHECK(load(text).ok() && text=="previous validated record\n");
    }
    CHECK(publish("replacement validated record\n").ok());
    std::string text; CHECK(load(text).ok() && text=="replacement validated record\n");
    const auto path=lease.metadata().path+name;
    auto corrupt=read(path); corrupt.back()='x'; write(path,corrupt);
    CHECK(load(text).outcome==Outcome::corrupt_manifest);
    write(path,"legacy record\n"); CHECK(load(text).ok() && text=="legacy record\n");
}
void protocol_durable_records(Fixture& fixture) {
    const auto root=fixture.path+L"\\durable-records"; seed_protocol(root);
    std::unique_ptr<Namespace> lease; CHECK(reopen(metadata_descriptor(root),lease).ok());
    Descriptor parsed;
    CHECK(parse_descriptor("sentinel-test-session-v3\nseed_hex=666978747572652d73656564\nteam=0\nslot=1\ngeneration_fingerprint="+
        std::string(64,'a')+"\nslot_data_revision=0.7.0\nsession_mod_contract_revision=2\nprovenance=synthetic-fixture\nroot=C:\\synthetic\n"
        "campaign=base\nstarting_stage=base_start\ndifficulty=1\nintent=create\n",parsed).ok());
    const auto base=checkpoint_contract(parsed,lease->metadata().namespace_id);
    CHECK(!parsed.campaign.unified && base.find("campaign=base\nstarting_stage=base_start\n")!=base.npos);
    using namespace sentinel::testing;
    protocol_record_replacement(*lease,false);
    protocol_record_replacement(*lease,true);
    bool cleared=false;
    CHECK(sentinel::save::recovery_clear(*lease,&cleared) && cleared);
    CHECK(lease->publish_recovery_record(false,"sentinel-recovery-v1\ntruncated\n").ok());
    cleared=false; CHECK(sentinel::save::recovery_clear(*lease,&cleared) && cleared);
    const auto legacy="sentinel-recovery-v1\nnamespace="+lease->metadata().namespace_id+
        "\nbackup=fixture\nquarantine=\nprevious_checkpoint_hex=\nprevious_selection_present=0\nprevious_selection_hex=\n";
    write(lease->metadata().path+L"\\recovery.pending",legacy);
    write(lease->metadata().path+L"\\recovery.complete",legacy);
    cleared=false; CHECK(sentinel::save::recovery_clear(*lease,&cleared) && !cleared);
    for (auto fault : {FileFault::after_write,FileFault::after_truncate,FileFault::after_flush}) {
        CHECK(lease->publish_restart_record(false,"previous validated record\n").ok());
        lease.reset();
        interrupt_protocol(root,L"restart",fault,L"\\stage.restart.pending");
        CHECK(reopen(metadata_descriptor(root),lease).ok());
        std::string text;
        CHECK(lease->restart_record(false,text).ok() && text=="previous validated record\n");
    }
}
void protocol_explicit_recovery(Fixture& fixture) {
    const auto root=fixture.path+L"\\explicit-recovery"; seed_protocol(root);
    std::unique_ptr<Namespace> lease; auto value=metadata_descriptor(root); CHECK(reopen(value,lease).ok());
    NativeDisk disk{fs::path(root)/"provider",{}};
    for (unsigned attempt=0;attempt<2;++attempt) {
        CheckpointPlan before; CHECK(lease->checkpoint_plan(value,before).ok());
        const auto directory=lease->metadata().native_root+"/GAME-AUTOSAVE0/";
        const auto details=read((disk.path/fs::path(directory+"game.details")).wstring());
        const auto duration=read((disk.path/fs::path(directory+"game_duration.dat")).wstring());
        CHECK(protocol_save(*lease,disk,L"",[] {}));
        CheckpointPlan candidate; CHECK(lease->checkpoint_plan(value,candidate).ok());
        CHECK(lease->discard_checkpoint(candidate.side).ok());
        disk.write(directory+"game.details",details); disk.write(directory+"game_duration.dat",duration);
        value.campaign.intent=CampaignIntent::recover; value.campaign.recovery_basename=candidate.archive;
        if (!attempt) {
            CheckpointWrite pending; CHECK(lease->begin_checkpoint(value,NativeDisk::owner(0),pending).ok());
            const auto missing=(disk.path/fs::path(directory+"game.details")).wstring();
            CHECK(DeleteFileW(missing.c_str()));
            const auto refused=sentinel::save::recover_campaign(*lease,value,disk.transport());
            CHECK(!refused.complete && !refused.mutated &&
                std::string_view(refused.reason)=="recovery_continuity_publication_failed");
            CHECK(!fs::exists(missing));
            std::string journal; CHECK(lease->recovery_record(false,journal).win32_error==ERROR_FILE_NOT_FOUND);
            CHECK(lease->discard_checkpoint(pending.side).ok());
            disk.write(directory+"game.details",details);
        }
        const auto recovered=sentinel::save::recover_campaign(*lease,value,disk.transport());
        if (!recovered.complete) std::fprintf(stderr,"explicit recovery: %s\n",recovered.reason);
        CHECK(recovered.complete && recovered.mutated);
        CheckpointPlan after; CHECK(lease->checkpoint_plan(value,after).ok() && after.checkpoint==before.checkpoint+1);
    }
    auto changed=value; changed.campaign.unified=false;
    CheckpointPlan plan; CHECK(lease->checkpoint_plan(changed,plan).ok() && plan.state==CheckpointState::identity_mismatch);
}
void protocol_faults(Fixture& fixture) {
    using sentinel::testing::FileFault;
    unsigned attempts = 0;
    const std::vector<std::pair<std::wstring, std::wstring>> windows{{L"begin", L"\\checkpoint-B.started"},
                                                                     {L"begin", L"\\checkpoint-B.pending"},
                                                                     {L"native", L"\\game.details"},
                                                                     {L"native", L"\\game_duration.dat"},
                                                                     {L"archive", L"\\payload-0.bin"},
                                                                     {L"archive", L"\\payload-1.bin"},
                                                                     {L"archive", L"\\transport.manifest"},
                                                                     {L"archive", L"\\automatic-*"},
                                                                     {L"stage", L"\\checkpoint-B.pending"},
                                                                     {L"receipt", L"\\checkpoint-B.receipt"},
                                                                     {L"selection", L"\\sentinel-selection-GAME.txt"},
                                                                     {L"commit", L"\\checkpoint-B.commit"},
                                                                     {L"migration", L"\\payload-0.bin"},
                                                                     {L"migration", L"\\payload-1.bin"},
                                                                     {L"migration", L"\\transport.manifest"},
                                                                     {L"migration", L"\\automatic-*"},
                                                                     {L"migration", L"\\checkpoint-A.started"},
                                                                     {L"migration", L"\\checkpoint-A.pending"},
                                                                     {L"migration", L"\\checkpoint-A.receipt"},
                                                                     {L"migration", L"\\checkpoint-A.commit"},
                                                                     {L"migration", L"\\campaign.checkpoint.legacy"},
                                                                     {L"migration", L"\\campaign.checkpoint"},
                                                                     {L"recovery", L"\\payload-0.bin"},
                                                                     {L"recovery", L"\\payload-1.bin"},
                                                                     {L"recovery", L"\\transport.manifest"},
                                                                     {L"recovery", L"\\recovery.pending"},
                                                                     {L"recovery", L"\\game.details"},
                                                                     {L"recovery", L"\\game_duration.dat"},
                                                                     {L"recovery", L"\\sentinel-selection-GAME.txt"},
                                                                     {L"recovery", L"\\recovery.complete"}};
    protocol_write_faults(fixture, attempts, windows);
    protocol_truncate_faults(fixture, attempts, windows);
    protocol_cleanup_faults(fixture, attempts);
    protocol_discard_faults(fixture, attempts);
    protocol_unproven_payloads(fixture);
    protocol_retention(fixture);
    protocol_future_format(fixture);
    protocol_auxiliary_change(fixture, attempts);
    protocol_first_receipt(fixture);
    protocol_archive_unavailable(fixture);
    protocol_durable_records(fixture);
    protocol_explicit_recovery(fixture);
    std::printf("PASS checkpoint protocol crash windows=%u; migration, payloads, receipt, selection, commit, recovery and retention\n",attempts);
}
