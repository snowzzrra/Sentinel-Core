// Copyright (c) 2026 snowzzrra. MIT; see ../LICENSE.
std::string sha256(std::string_view bytes) {
    BCRYPT_ALG_HANDLE algorithm=nullptr; BCRYPT_HASH_HANDLE hash=nullptr;
    std::array<unsigned char,32> output{};
    CHECK(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0);
    CHECK(BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)>=0);
    CHECK(BCryptHashData(hash,reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())),static_cast<ULONG>(bytes.size()),0)>=0);
    CHECK(BCryptFinishHash(hash,output.data(),32,0)>=0);
    CHECK(BCryptDestroyHash(hash)>=0 && BCryptCloseAlgorithmProvider(algorithm,0)>=0);
    std::string result;
    for (auto byte:output) { result+="0123456789abcdef"[byte>>4]; result+="0123456789abcdef"[byte&15]; }
    return result;
}
std::string hex_bytes(std::string_view bytes) {
    std::string result;
    for (unsigned char byte:bytes) { result+="0123456789abcdef"[byte>>4]; result+="0123456789abcdef"[byte&15]; }
    return result;
}
std::string signed_record(const std::string& bytes) { return bytes+"sha256="+sha256(bytes)+"\n"; }
void checkpoint_parser_cases(const std::string& legacy, const std::string& contract, const std::string& directory) {
    CheckpointRecord parsed;
    CHECK(parse_checkpoint(legacy, contract, directory, parsed).ok() && parsed.subtype == 2 &&
          parsed.files.size() == 2);
    auto without_subtype = legacy;
    without_subtype.erase(without_subtype.find("subtype=2\n"), 10);
    CHECK(parse_checkpoint(without_subtype, contract, directory, parsed).ok() && parsed.subtype == 1);
    for (const auto& pair :
         std::vector<std::pair<std::string, std::string>>{{"checkpoint=1", "checkpoint=18446744073709551616"},
                                                          {"files=2", "files=02"},
                                                          {"game_duration.dat", "GAME.DETAILS"},
                                                          {"game_duration.dat", "../outside"},
                                                          {"map=game/hub/hub", "map=../outside"},
                                                          {"subtype=2", "subtype=3"}}) {
        auto bad = legacy;
        bad.replace(bad.find(pair.first), pair.first.size(), pair.second);
        CHECK(!parse_checkpoint(bad, contract, directory, parsed).ok());
    }
    for (size_t end = 0; end < legacy.size(); ++end)
        CHECK(!parse_checkpoint(std::string_view(legacy).substr(0, end), contract, directory, parsed).ok());
}
void expect_checkpoint_state(Namespace& owner, const Descriptor& value, CheckpointPlan& plan,
                             CheckpointState expected) {
    CHECK(owner.checkpoint_plan(value, plan).ok());
    if (plan.state != expected)
        std::fprintf(stderr, "checkpoint state=%s expected=%s number=%llu\n", checkpoint_state_name(plan.state),
                     checkpoint_state_name(expected), plan.checkpoint);
    CHECK(plan.state == expected);
}
void checkpoint_readers(Fixture& fixture) {
    auto value=metadata_descriptor(fixture.path+L"\\ap"); value.identity.slot=51;
    std::unique_ptr<Namespace> owner; CHECK(prepare(value,owner).ok());
    const auto& m=owner->metadata();
    const auto contract=sentinel::save::campaign_contract(value,m.namespace_id);
    CheckpointPlan plan;
    const auto state = [&](CheckpointState expected) { expect_checkpoint_state(*owner, value, plan, expected); };
    state(CheckpointState::missing);
    CHECK(owner->publish_campaign_record(false,contract,true).ok());
    state(CheckpointState::interrupted);
    Backup output; TransportSource source; source.attempt=&output;
    auto metadata=transport_metadata(*owner,source);
    metadata.directory=m.native_root+"/GAME-AUTOSAVE0";
    metadata.files[1].name="game_duration.dat";
    metadata.steam_user=76561198000000001ull; metadata.provider=sentinel::save::recovery_provider;
    metadata.contract=contract;
    const auto checkpoint=[&](uint64_t number) {
        std::string text=contract+"checkpoint="+std::to_string(number)+"\nmap=game/hub/hub\nsubtype=2\nfiles=2\n";
        for (size_t i=0;i<source.bytes.size();++i) text+="file="+metadata.directory+"/"+metadata.files[i].name+","+
            std::to_string(source.bytes[i].size())+","+sha256(source.bytes[i])+"\n";
        return text+"state=native_saved_readback_verified\n";
    };
    const auto legacy=checkpoint(1);
    CHECK(owner->publish_campaign_record(true,legacy,true).ok()); state(CheckpointState::confirmed);
    CHECK(!plan.ab && plan.checkpoint==1);
    checkpoint_parser_cases(legacy, contract, metadata.directory);
    CHECK(owner->publish_campaign_record(true,contract+"state=native_save_pending\n",false).ok()); state(CheckpointState::interrupted);
    CHECK(owner->publish_campaign_record(true,"sentinel-checkpoint-ab-v2\n",false).ok()); state(CheckpointState::unsupported);
    CHECK(owner->publish_campaign_record(true,"sentinel-checkpoint-ab-v1\n",false).ok()); state(CheckpointState::interrupted);
    std::string previous;
    std::array<std::string,2> pending,receipt,commit;
    std::array<std::wstring,2> archives;
    for (unsigned i=0;i<2;++i) {
        const char side=static_cast<char>('A'+i);
        metadata.checkpoint=checkpoint(i+1); metadata.operation_id=i+1;
        CHECK(owner->backup_transport(metadata,TransportSource::copy,&source,output).ok());
        archives[i]=output.path;
        const auto basename=fs::path(output.path).filename().string();
        pending[i]=signed_record("sentinel-checkpoint-pending-v1\nnamespace="+m.namespace_id+"\ngeneration="+
            value.identity.generation_fingerprint+"\ncontract_sha256="+sha256(contract)+"\nnative_root="+m.native_root+
            "\nsteam_user="+std::to_string(metadata.steam_user)+"\nprovider="+metadata.provider+"\ncheckpoint="+std::to_string(i+1)+
            "\ntransaction="+std::string(32,static_cast<char>('a'+i))+"\nside="+side+"\npredecessor="+(i?previous:std::string(64,'0'))+
            "\narchive="+basename+"\nmanifest_sha256="+sha256(read(output.path+L"\\transport.manifest"))+
            "\ncheckpoint_hex="+hex_bytes(metadata.checkpoint)+"\n");
        const auto pending_hash=pending[i].substr(pending[i].size()-65,64);
        receipt[i]=signed_record("sentinel-checkpoint-receipt-v1\npending_sha256="+pending_hash+
            "\nnative_success=1\ncallbacks=1\nreadback_terminal=1\ntransition=1\n");
        const auto receipt_hash=receipt[i].substr(receipt[i].size()-65,64);
        commit[i]=signed_record("sentinel-checkpoint-commit-v1\npending_sha256="+pending_hash+"\nreceipt_sha256="+receipt_hash+"\n");
        previous=commit[i].substr(commit[i].size()-65,64);
        const auto stem=m.path+L"\\checkpoint-"+static_cast<wchar_t>(side);
        write(stem+L".pending",pending[i]); state(CheckpointState::interrupted);
        write(stem+L".receipt",receipt[i]); state(CheckpointState::receipt_ready);
        CHECK(plan.side==side && plan.checkpoint==i+1);
        write(stem+L".commit",commit[i]); state(CheckpointState::confirmed);
        CHECK(plan.ab && plan.side==side && plan.checkpoint==i+1);
    }
    auto bad=pending[1]; const auto at=bad.find("predecessor=")+12;
    bad[at]=bad[at]=='0'?'1':'0'; bad=signed_record(bad.substr(0,bad.size()-72));
    auto bad_receipt=signed_record("sentinel-checkpoint-receipt-v1\npending_sha256="+bad.substr(bad.size()-65,64)+
        "\nnative_success=1\ncallbacks=1\nreadback_terminal=1\ntransition=1\n");
    write(m.path+L"\\checkpoint-B.pending",bad); write(m.path+L"\\checkpoint-B.receipt",bad_receipt);
    state(CheckpointState::conflict);
    write(m.path+L"\\checkpoint-B.pending",pending[1]); write(m.path+L"\\checkpoint-B.receipt",receipt[1]);
    for (const auto& field:{std::string("namespace="),std::string("generation="),std::string("contract_sha256="),
            std::string("native_root="),std::string("provider="),std::string("steam_user=")}) {
        auto altered=pending[1]; const auto position=altered.find(field)+field.size(); CHECK(position>=field.size());
        altered[position]=altered[position]=='1'?'2':'1';
        altered=signed_record(altered.substr(0,altered.size()-72));
        write(m.path+L"\\checkpoint-B.pending",altered); state(CheckpointState::identity_mismatch);
        write(m.path+L"\\checkpoint-B.pending",pending[1]);
    }
    for (const auto& entry:std::vector<std::pair<std::wstring,std::string>>{
        {L".pending",pending[1]},{L".receipt",receipt[1]},{L".commit",commit[1]}}) {
        const auto path=m.path+L"\\checkpoint-B"+entry.first;
        for (const auto end:{size_t(0),entry.second.size()/2,entry.second.size()-1}) {
            write(path,entry.second.substr(0,end));
            CHECK(owner->checkpoint_plan(value,plan).ok()); CHECK(plan.state!=CheckpointState::confirmed && plan.checkpoint<=2);
        }
        write(path,entry.second); state(CheckpointState::confirmed);
    }
    write(archives[1]+L"\\payload-0.bin","abd"); state(CheckpointState::interrupted); CHECK(plan.side=='A');
    write(archives[0]+L"\\payload-0.bin","abd"); state(CheckpointState::invalid);
    CHECK(owner->publish_campaign_record(true,legacy,false).ok()); state(CheckpointState::confirmed);
    auto foreign=value; foreign.campaign.difficulty=2;
    CHECK(owner->checkpoint_plan(foreign,plan).ok() && plan.state==CheckpointState::identity_mismatch);
    std::puts("PASS checkpoint readers: legacy, A/B hashes, receipts, sequence conflicts, truncation, payload corruption");
}
void archive_cost(Fixture& fixture) {
    auto value=descriptor(fixture.path+L"\\ap"); value.identity.slot=52;
    value.campaign={CampaignIntent::resume,1};
    std::unique_ptr<Namespace> owner; CHECK(prepare(value,owner).ok());
    Backup output; TransportSource source; source.attempt=&output;
    source.bytes={std::string(4*1024*1024+128,'a'),std::string(4*1024*1024+256,'b')};
    auto metadata=transport_metadata(*owner,source); metadata.directory=owner->metadata().native_root+"/GAME-AUTOSAVE0";
    metadata.files[1].name="game_duration.dat"; metadata.automatic=true;
    metadata.steam_user=76561198000000001ull; metadata.provider=sentinel::save::recovery_provider;
    metadata.contract=sentinel::save::campaign_contract(value,owner->metadata().namespace_id);
    CHECK(owner->publish_campaign_record(false,metadata.contract,true).ok());
    LARGE_INTEGER frequency{}; CHECK(QueryPerformanceFrequency(&frequency));
    for (unsigned i=0;i<3;++i) {
        LARGE_INTEGER start{},end{}; CHECK(QueryPerformanceCounter(&start));
        CheckpointWrite transaction; CHECK(owner->begin_checkpoint(value,metadata.steam_user,transaction).ok());
        metadata.checkpoint=metadata.contract+"checkpoint="+std::to_string(transaction.number)+"\nmap=game/hub/hub\nsubtype=1\nfiles=2\n";
        for (size_t n=0;n<source.bytes.size();++n) metadata.checkpoint+="file="+metadata.directory+"/"+metadata.files[n].name+","+
            std::to_string(source.bytes[n].size())+","+sha256(source.bytes[n])+"\n";
        metadata.checkpoint+="state=native_saved_readback_verified\n";
        const auto copy=[](void* opaque,size_t file,uint32_t offset,char* bytes,uint32_t count) {
            const auto& input=*static_cast<TransportSource*>(opaque);
            std::memcpy(bytes,input.bytes.at(file).data()+offset,count); return true;
        };
        CHECK(owner->backup_transport(metadata,copy,&source,output).ok());
        CHECK(owner->stage_checkpoint(transaction,fs::path(output.path).filename().wstring()).ok());
        CHECK(owner->receipt_checkpoint(transaction).ok() && owner->commit_checkpoint(transaction).ok());
        CHECK(owner->prune_checkpoints().ok());
        CHECK(QueryPerformanceCounter(&end));
        uint64_t space=0;
        for (const auto& file:fs::directory_iterator(output.path)) space+=fs::file_size(file);
        CHECK(output.files==2 && output.bytes==source.bytes[0].size()+source.bytes[1].size() && space>output.bytes);
        std::unique_ptr<TransportArchive> archive; CHECK(owner->reopen_transport(fs::path(output.path).filename().wstring(),archive).ok());
        std::printf("checkpoint_storage payload_bytes=%llu elapsed_us=%llu file_bytes=%llu\n",output.bytes,
            static_cast<uint64_t>((end.QuadPart-start.QuadPart)*1000000/frequency.QuadPart),space);
    }
}
int archive_child(const wchar_t* root,const wchar_t* number,const wchar_t* stem,const wchar_t* suffix) {
    using namespace sentinel::testing;
    FILE* diagnostics=nullptr;
    CHECK(!_wfreopen_s(&diagnostics,(std::wstring(root)+L"\\child.stderr").c_str(),L"w",stderr));
    const auto mode=static_cast<FileFault>(std::wcstoul(number,nullptr,10));
    HANDLE entered=OpenEventW(EVENT_MODIFY_STATE,FALSE,(std::wstring(stem)+L"-entered").c_str());
    HANDLE release=OpenEventW(SYNCHRONIZE,FALSE,(std::wstring(stem)+L"-release").c_str()); CHECK(entered && release);
    std::unique_ptr<Namespace> owner; CHECK(reopen(metadata_descriptor(root),owner).ok());
    Backup output; TransportSource source; source.attempt=&output;
    source.bytes={std::string(131073,'a'),std::string(65537,'b')};
    auto metadata=transport_metadata(*owner,source);
    metadata.directory=owner->metadata().native_root+"/GAME-AUTOSAVE0";
    file_fault(mode,suffix,entered,release);
    const auto result=owner->backup_transport(metadata,TransportSource::copy,&source,output);
    if (result.ok() || file_fault_hits()!=1) std::fprintf(stderr,"archive outcome=%s hits=%u suffix=%ls path=%ls\n",outcome_name(result.outcome),file_fault_hits(),suffix,output.path.c_str());
    CHECK(!result.ok() && file_fault_hits()==1);
    file_fault(FileFault::none,L"");
    CHECK(CloseHandle(entered) && CloseHandle(release)); return 0;
}
void interrupted_archives(Fixture& fixture) {
    using sentinel::testing::FileFault;
    unsigned attempts=0;
    for (const auto* suffix:{L"\\payload-0.bin",L"\\payload-1.bin",L"\\transport.manifest"}) {
        for (const auto mode:{FileFault::short_write,FileFault::flush,FileFault::after_write,FileFault::after_flush}) {
            const auto root=fixture.path+L"\\archive-fault-"+std::to_wstring(++attempts); CHECK(CreateDirectoryW(root.c_str(),nullptr));
            const auto previous=seed_checkpoint(root);
            interrupt_metadata(root,mode,suffix);
            std::unique_ptr<Namespace> owner; const auto value=metadata_descriptor(root); CHECK(reopen(value,owner).ok());
            CheckpointPlan plan; CHECK(owner->checkpoint_plan(value,plan).ok() && plan.state==CheckpointState::confirmed && !plan.ab && plan.checkpoint==1);
            std::string record; CHECK(owner->campaign_record(true,record).ok() && record==previous);
            unsigned archives=0;
            for (const auto& path:fs::directory_iterator(root)) {
                const auto name=path.path().filename().wstring();
                if (name.find(L"transport-backup-")!=0) continue;
                ++archives; std::unique_ptr<TransportArchive> archive;
                const auto result=owner->reopen_transport(name,archive);
                if (result.ok()) CHECK(archive && archive->metadata().steam_user==0);
                else CHECK(!archive);
            }
            CHECK(archives==1);
        }
    }
    std::printf("PASS archive interruption windows=%u; active legacy checkpoint preserved\n",attempts);
    archive_cost(fixture);
}
