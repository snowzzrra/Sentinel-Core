#include "installation_probe.h"
#include <cstdio>
#include <string>
namespace {
const char* stage(uint32_t n) {
 constexpr const char* names[] = {"not_attempted","prelaunch_descriptor","module_pin","native_target",
  "native_binding","minhook_initialize","native_create","native_enable","save_binding","save_target",
  "reference_copy","factory_call","steam_context_import","save_create","policy_publication","save_enable",
  "installation_ready","cleanup_remove","cleanup_uninitialize","startup","upstream"};
 return n < sizeof(names)/sizeof(names[0]) ? names[n] : "unknown";
}
std::string value(uint32_t n, bool signed_value = false) {
 return n == SC_INSTALL_UNKNOWN ? "null" : (signed_value ? std::to_string(static_cast<int32_t>(n)) : std::to_string(n));
}
void event(const sc_install_event& e) {
 if (!e.sequence) { std::printf("null"); return; }
 char expected[65]{}, actual[65]{};
 for (uint32_t i = 0; i < e.byte_count; ++i) {
  std::snprintf(expected+i*2,3,"%02x",e.expected_bytes[i]); std::snprintf(actual+i*2,3,"%02x",e.actual_bytes[i]);
 }
 const auto name = e.target_group == 1 ? std::string("native_target_")+std::to_string(e.target_index) :
  e.target_group == 2 ? std::string("save_target_")+std::to_string(e.target_index) : std::string(stage(e.stage));
 std::printf("{\"sequence\":\"%llu\",\"at_ms\":\"%llu\",\"duration_ms\":\"%llu\",\"stage\":\"%s\","
  "\"target_group\":%u,\"target_index\":%s,\"target_name\":\"%s\",\"rva\":%u,\"signature_offset\":%u,"
  "\"result\":\"%s\",\"reason\":\"%s\",\"read_reason\":%s,\"win32_error\":%s,\"minhook_status\":%s,"
  "\"byte_count\":%u,\"collision_rva\":%u,\"byte_window_offset\":%u,\"expected_bytes\":\"%s\",\"actual_bytes\":\"%s\"}",
  e.sequence,e.at_ms,e.duration_ms,stage(e.stage),e.target_group,value(e.target_index).c_str(),name.c_str(),e.rva,e.signature_offset,
  e.result==0?"pending":e.result==1?"completed":"failed",sentinel::native_reason_name(e.reason),
  value(e.read_reason).c_str(),value(e.win32_error).c_str(),value(e.minhook_status,true).c_str(),e.byte_count,e.collision_rva,e.byte_window_offset,expected,actual);
}
}
void print_installation(const sentinel::Inspection& r, bool json) {
 const auto& s=r.snapshot; const auto& v=r.installation;
 if (!json) {
  std::printf("Installation ABI 1 | build=%s | phase=%u | validated=%u created=%u enabled=%u\n"
   "First failure: %s target=%u RVA=0x%x reason=%s MinHook=%s Win32=%s\n"
   "Use --json for retained stages, timing, cleanup, and startup evidence. Installation ready is not AP admission.\n",
   s.core.build_id,v.phase,v.validated,v.created,v.enabled,stage(v.primary_failure.stage),v.primary_failure.target_index,
   v.primary_failure.rva,sentinel::native_reason_name(v.primary_failure.reason),
   v.primary_failure.sequence?value(v.primary_failure.minhook_status,true).c_str():"not_attempted",
   v.primary_failure.sequence?value(v.primary_failure.win32_error).c_str():"not_attempted");
  return;
 }
 std::printf("{\"result\":\"ok\",\"operation\":\"save_installation\",\"core_version\":\"%s\",\"build_id\":\"%s\","
  "\"target_pid\":%u,\"server_pid\":%u,\"process_created\":\"%llu\",\"instance_id\":\"%s\","
  "\"installation\":{\"abi\":1,\"clock\":\"GetTickCount64\",\"units\":\"milliseconds\","
  "\"attempt\":%u,\"sequence\":\"%llu\",\"phase\":\"%s\",\"last_completed_stage\":\"%s\","
  "\"startup_observation\":\"%s\",\"validated\":%u,\"created\":%u,\"enabled\":%u,"
  "\"cleanup_failures\":%u,\"gaps\":%u,\"active\":",
  s.core.version,s.core.build_id,s.pid,r.server_pid,s.process_created,sentinel::instance_text(s.instance).c_str(),
  v.attempt,v.sequence,v.phase==0?"not_attempted":v.phase==1?"running":v.phase==2?"failed":"installation_ready",
  stage(v.last_completed_stage),v.startup_observation==0?"not_observed":v.startup_observation==1?"witnessed":
  v.startup_observation==2?"missed_at_import":"stopped_before_witness",v.validated,v.created,v.enabled,v.cleanup_failures,v.gaps);
 event(v.active); std::printf(",\"primary_failure\":"); event(v.primary_failure);
 std::printf(",\"cleanup_failure\":"); event(v.cleanup_failure); std::puts("}}");
}
