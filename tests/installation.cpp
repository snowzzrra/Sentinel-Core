#include "native_target.h"
#include "save_session.h"
#include "save_native_hooks.h"
#include "save_campaign_native.h"
#include "MinHook.h"
#include "protocol.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
using namespace sentinel;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"FAIL installation.cpp:%d %s win32=%lu\n",__LINE__,#x,GetLastError()); std::exit(1); } } while(0)
struct Memory final : engine::Memory {
 engine::LocalMemory local; uintptr_t fail = 0, zero = 0;
 engine::ReadResult copy(uintptr_t address, void* out, size_t count) override {
  if (address == zero) { std::memset(out,0,count); return {}; }
  return address == fail ? engine::ReadResult{77,ERROR_PARTIAL_COPY} : local.copy(address,out,count);
 }
};
void contract() {
 auto bytes=static_cast<uint8_t*>(VirtualAlloc(nullptr,0x4000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)); CHECK(bytes);
 const auto base=reinterpret_cast<uintptr_t>(bytes);
 engine::Image image; image.base=base; image.size=0x4000;
 image.sections={{0,0x2000,IMAGE_SCN_MEM_READ|IMAGE_SCN_MEM_EXECUTE},{0x2000,0x2000,IMAGE_SCN_MEM_READ}};
 RUNTIME_FUNCTION table[]={{0x100,0x106,0x2000},{0x106,0x180,0x2010}};
 bytes[0x2000]=1; bytes[0x2010]=1|(UNW_FLAG_CHAININFO<<3); std::memcpy(bytes+0x2014,&table[0],12);
 const uint8_t prefix[]={0x55,0x48,0x89,0xe5,0x90,0x90};
 std::memcpy(bytes+0x100,prefix,sizeof(prefix));
 for(unsigned i=6;i<32;++i) bytes[0x100+i]=static_cast<uint8_t>(i+0x40);
 for(unsigned i=0;i<32;++i) bytes[0x120+i]=static_cast<uint8_t>(i+0xa0);
 native::Target target; target.address=base+0x100; target.signature_offset=32;
 std::memcpy(target.bytes.data(),bytes+0x100,32); std::memcpy(target.signature.data(),bytes+0x120,32);
 DWORD previous=0; CHECK(VirtualProtect(bytes,0x2000,PAGE_EXECUTE_READ,&previous));
 CHECK(RtlAddFunctionTable(table,2,base));
 HANDLE stop=CreateEventW(nullptr,TRUE,FALSE,nullptr); CHECK(stop);
 Memory memory; native::ValidationDetail detail;
 CHECK(native::validate_target(memory,image,target,stop,GetTickCount64()+1000,&detail)==SC_NATIVE_NONE);
 bytes[0x2010]=1; // Neighboring fragment without the chain does not belong to the entry.
 CHECK(native::validate_target(memory,image,target,stop,GetTickCount64()+1000,&detail)==SC_NATIVE_TARGET_BOUNDARY);
 bytes[0x2010]=1|(UNW_FLAG_CHAININFO<<3);
 auto cyclic=table[1]; std::memcpy(bytes+0x2014,&cyclic,12);
 CHECK(native::validate_target(memory,image,target,stop,GetTickCount64()+1000,&detail)==SC_NATIVE_TARGET_BOUNDARY);
 std::memcpy(bytes+0x2014,&table[0],12);
 auto interior=target; ++interior.address;
 CHECK(native::validate_target(memory,image,interior,stop,GetTickCount64()+1000,&detail)!=SC_NATIVE_NONE);
 CHECK(VirtualProtect(bytes,0x2000,PAGE_READWRITE,&previous)); std::memcpy(bytes+0x800,target.signature.data(),32);
 CHECK(VirtualProtect(bytes,0x2000,PAGE_EXECUTE_READ,&previous));
 save::Installation record;
 CHECK(native::validate_recorded(record,memory,image,target,stop,GetTickCount64()+1000,2,3)==SC_NATIVE_TARGET_NOT_UNIQUE);
 auto first=record.inspect().primary_failure;
 CHECK(first.target_index==3 && first.rva==0x100 && first.signature_offset==32 && first.collision_rva==0x800);
 CHECK(first.minhook_status==SC_INSTALL_UNKNOWN && first.read_reason==0);
 CHECK(record.hook(SC_INSTALL_REMOVE,2,0,0x100,[]{return 10;})==10);
 auto retained=record.inspect();
 CHECK(retained.primary_failure.sequence==first.sequence && retained.cleanup_failure.minhook_status==10 && retained.cleanup_failures==1);
 CHECK(retained.validated==0 && retained.created==0 && retained.enabled==0);
 for(auto stage:{SC_INSTALL_MH_INITIALIZE,SC_INSTALL_NATIVE_CREATE,SC_INSTALL_NATIVE_ENABLE,SC_INSTALL_SAVE_CREATE,SC_INSTALL_SAVE_ENABLE}) {
  save::Installation failure; CHECK(failure.hook(stage,2,9,0x17e6c40,[]{return -1;})==-1);
  CHECK(failure.inspect().primary_failure.minhook_status==UINT32_MAX);
  CHECK(failure.inspect().primary_failure.minhook_status!=SC_INSTALL_UNKNOWN);
  CHECK(failure.inspect().primary_failure.win32_error==SC_INSTALL_UNKNOWN);
 }
 save::Installation counts;
 CHECK(MH_Initialize()==MH_OK);
 save::Installation real_api;
 CHECK(real_api.hook(SC_INSTALL_SAVE_CREATE,2,9,0, [&]{return MH_CreateHook(bytes+0x2000,bytes+0x100,nullptr);})==MH_ERROR_NOT_EXECUTABLE);
 CHECK(real_api.inspect().primary_failure.minhook_status==MH_ERROR_NOT_EXECUTABLE);
 save::Installation real_enable;
 CHECK(real_enable.hook(SC_INSTALL_SAVE_ENABLE,2,9,0,[&]{return MH_EnableHook(bytes+0x100);})==MH_ERROR_NOT_CREATED);
 CHECK(real_enable.inspect().primary_failure.minhook_status==MH_ERROR_NOT_CREATED);
 CHECK(MH_Uninitialize()==MH_OK);
 CHECK(!counts.hook(SC_INSTALL_SAVE_CREATE,2,9,0x17e6c40,[]{return 0;}));
 CHECK(counts.hook(SC_INSTALL_SAVE_ENABLE,2,9,0x17e6c40,[]{return 10;})==10);
 CHECK(counts.inspect().created==1 && counts.inspect().enabled==0);
 for(auto stage:{SC_INSTALL_REFERENCE_COPY,SC_INSTALL_FACTORY_CALL,SC_INSTALL_STEAM_IMPORT}) {
  save::Installation helper; auto event=helper.begin(stage,3,SC_INSTALL_UNKNOWN,0x367510);
  save::attach_read(event,{77,ERROR_PARTIAL_COPY}); helper.finish(event,SC_NATIVE_READ_FAILED);
  CHECK(helper.inspect().primary_failure.read_reason==77 && helper.inspect().primary_failure.win32_error==ERROR_PARTIAL_COPY);
 }
 save::Installation unreadable; memory.fail=target.address;
 CHECK(native::validate_recorded(unreadable,memory,image,target,stop,GetTickCount64()+1000,2,0)==SC_NATIVE_TARGET_BYTES);
 CHECK(unreadable.inspect().primary_failure.win32_error==ERROR_PARTIAL_COPY);
 save::Installation startup; startup.startup(2);
 CHECK(startup.inspect().startup_observation==2 && startup.inspect().primary_failure.stage==SC_INSTALL_STARTUP);
 save::Session owner; CHECK(owner.state()==save::SessionState::disabled && !owner.native_io());
 owner.install(1,2,save::required_routes); CHECK(!owner.native_io()); // Record/readiness never admits.
 Snapshot identity{}; identity.core.abi_version=1; strcpy_s(identity.core.version,"0.6.0"); strcpy_s(identity.core.build_id,"fixture");
 identity.pid=123; identity.process_created=456; identity.instance[0]=1;
 Message wire{}; const auto size=encode_installation_response(wire,WireResult::ok,identity,retained);
 Snapshot decoded; sc_save_installation_snapshot result{}; WireResult status;
 CHECK(size && size<=max_message && decode_installation_response(wire,size,status,decoded,result));
 CHECK(result.primary_failure.sequence==first.sequence && result.cleanup_failure.minhook_status==10 && decoded.pid==123);
 CHECK(!decode_installation_response(wire,size-1,status,decoded,result));
 uint16_t operation=0; const auto request=encode_request(wire,save_installation_capability,wire_version,save_installation_operation);
 CHECK(decode_request(wire,request,&operation)==WireResult::ok && operation==save_installation_operation);
 CHECK(RtlDeleteFunctionTable(table)); CloseHandle(stop); CHECK(VirtualFree(bytes,0,MEM_RELEASE));
 std::puts("PASS chained ownership, foreign fragment/cycle/duplicate/interior refusal, first target/read/API statuses, cleanup retention, startup, additive wire; controlled host only");
}
int wmain(int argc,wchar_t** argv) {
 if(argc==1) {contract();return 0;}
 CHECK(argc==2);
 HANDLE file=CreateFileW(argv[1],GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr); CHECK(file!=INVALID_HANDLE_VALUE);
 // Image data only: no loader, imports, entry point, hooks, game process, or save I/O.
 HANDLE mapping=CreateFileMappingW(file,nullptr,PAGE_READONLY|SEC_IMAGE_NO_EXECUTE,0,0,nullptr); CHECK(mapping);
 auto bytes=static_cast<uint8_t*>(MapViewOfFile(mapping,FILE_MAP_READ,0,0,0)); CHECK(bytes);
 Memory memory; engine::Image image; CHECK(!engine::read_image(memory,reinterpret_cast<uintptr_t>(bytes),image).reason);
 const auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(bytes);
 const auto nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(bytes+dos->e_lfanew);
 const auto& directory=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
 auto entries=reinterpret_cast<RUNTIME_FUNCTION*>(bytes+directory.VirtualAddress);
 CHECK(RtlAddFunctionTable(entries,directory.Size/sizeof(RUNTIME_FUNCTION),image.base));
 HANDLE stop=CreateEventW(nullptr,TRUE,FALSE,nullptr); CHECK(stop);
 save::Installation record;
 auto baseline_target=native::save_target(image.base,3); baseline_target.signature_offset=0;
 save::Installation baseline;
 CHECK(native::validate_recorded(baseline,memory,image,baseline_target,stop,GetTickCount64()+3000,2,3)==SC_NATIVE_TARGET_NOT_UNIQUE);
 std::puts("REPRODUCED baseline save target 3: target_not_unique on supported disk bytes");
 for(unsigned i=0;i<45;++i) {
  const auto reason=native::validate_recorded(record,memory,image,native::save_target(image.base,i),stop,GetTickCount64()+3000,2,i);
  if(reason) {auto e=record.inspect().primary_failure;std::fprintf(stderr,"TARGET index=%u reason=%u read=%u rva=%x offset=%u collision=%x\n",i,reason,e.read_reason,e.rva,e.signature_offset,e.collision_rva);}
  CHECK(reason==SC_NATIVE_NONE);
 }
 CHECK(native::function_window(memory,image,image.base+0x148dd10,image.base+0x148de8c,5));
 for(auto rva:{0x367510u,0x67473fu,0x14897adu,0x1be4b35u,0x2a1cc60u}) {
  save::Installation refused; memory.fail=image.base+rva; uintptr_t initializer=0;
  CHECK(!save::validate_native_helpers(refused,memory,image,initializer));
  const auto e=refused.inspect().primary_failure;
  CHECK(e.rva==rva && e.read_reason==77 && e.win32_error==ERROR_PARTIAL_COPY);
  CHECK(!initializer && refused.inspect().created==0 && refused.inspect().enabled==0);
 }
 memory.fail=0; memory.zero=image.base+0x2a1cc60;
 save::Installation null_import; uintptr_t initializer=0;
 CHECK(!save::validate_native_helpers(null_import,memory,image,initializer));
 CHECK(null_import.inspect().primary_failure.stage==SC_INSTALL_STEAM_IMPORT && null_import.inspect().primary_failure.read_reason==0);
 memory.zero=0; save::Installation helpers; CHECK(save::validate_native_helpers(helpers,memory,image,initializer));
 save::Installation campaign;
 for(const auto& target:save::campaign_targets(image.base))
  CHECK(native::validate_recorded(campaign,memory,image,target,stop,GetTickCount64()+3000,4,campaign.inspect().validated)==SC_NATIVE_NONE);
 std::printf("PASS supported disk image: %u save targets, %u campaign/UI targets and reference-copy caller; nonexecuting mapping, no loaded-game claim\n",record.inspect().validated,campaign.inspect().validated);
 CHECK(RtlDeleteFunctionTable(entries)); CloseHandle(stop); UnmapViewOfFile(bytes); CloseHandle(mapping); CloseHandle(file);
}
