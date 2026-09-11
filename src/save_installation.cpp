#include "save_installation.h"
#include <cstdio>
namespace sentinel::save {
void Installation::initialize() {
 wchar_t value[2]{};
 const bool mirror = GetEnvironmentVariableW(L"SENTINEL_INSTALL_DEBUG", value, 2) == 1 && value[0] == L'1';
 AcquireSRWLockExclusive(&lock_);
 if (!state_.attempt) {
  state_.size = sizeof(state_); state_.abi_version = 1; state_.attempt = 1; mirror_ = mirror;
 }
 ReleaseSRWLockExclusive(&lock_);
}
sc_install_event Installation::begin(uint32_t stage, uint32_t group, uint32_t index, uint32_t rva, uint32_t offset) {
 sc_install_event event{};
 event.at_ms = GetTickCount64(); event.stage = stage; event.target_group = group;
 event.target_index = index; event.rva = rva; event.signature_offset = offset;
 event.read_reason = event.win32_error = event.minhook_status = SC_INSTALL_UNKNOWN;
 AcquireSRWLockExclusive(&lock_);
 if (!state_.attempt) { state_.size = sizeof(state_); state_.abi_version = 1; state_.attempt = 1; }
 event.sequence = ++state_.sequence; state_.active = event;
 if (!state_.primary_failure.sequence) state_.phase = 1;
 ReleaseSRWLockExclusive(&lock_);
 return event;
}
void Installation::finish(sc_install_event event, uint32_t reason, uint32_t minhook, uint32_t win32) {
 event.duration_ms = GetTickCount64() - event.at_ms;
 event.reason = reason; event.result = reason ? 2u : 1u; event.minhook_status = minhook;
 if (win32 != SC_INSTALL_UNKNOWN) event.win32_error = win32;
 const bool cleanup = event.stage == SC_INSTALL_REMOVE || event.stage == SC_INSTALL_UNINITIALIZE;
 bool mirror = false;
 AcquireSRWLockExclusive(&lock_);
 if (state_.active.sequence == event.sequence) state_.active = event; else ++state_.gaps;
 if (reason) {
  if (cleanup) { ++state_.cleanup_failures; if (!state_.cleanup_failure.sequence) state_.cleanup_failure = event; }
  else if (!state_.primary_failure.sequence) { state_.primary_failure = event; mirror = mirror_; }
  state_.phase = 2;
 } else {
  if (!cleanup && !state_.primary_failure.sequence) state_.last_completed_stage = event.stage;
  if (event.stage == SC_INSTALL_SAVE_TARGET || event.stage == SC_INSTALL_NATIVE_TARGET) ++state_.validated;
  if (event.stage == SC_INSTALL_SAVE_CREATE || event.stage == SC_INSTALL_NATIVE_CREATE) ++state_.created;
  if (event.stage == SC_INSTALL_SAVE_ENABLE || event.stage == SC_INSTALL_NATIVE_ENABLE) ++state_.enabled;
  if (event.stage == SC_INSTALL_READY && !state_.primary_failure.sequence) { state_.phase = 3; mirror = mirror_; }
 }
 ReleaseSRWLockExclusive(&lock_);
 // At most first failure + installation-ready. No frame/callback logging.
 if (mirror) {
  char text[224]{};
  std::snprintf(text, sizeof(text), "[Sentinel Install] seq=%llu stage=%u group=%u target=%u rva=%x reason=%u minhook=%u win32=%u (2147483648=not_attempted)\n",
   event.sequence, event.stage, event.target_group, event.target_index, event.rva, reason, minhook, event.win32_error);
  OutputDebugStringA(text);
 }
}
void Installation::startup(uint32_t observation) {
 AcquireSRWLockExclusive(&lock_);
 if (observation != 3 || !state_.startup_observation) state_.startup_observation = observation;
 if (observation == 2 && !state_.primary_failure.sequence) {
  sc_install_event event{}; event.sequence = ++state_.sequence; event.at_ms = GetTickCount64();
  event.stage = SC_INSTALL_STARTUP; event.result = 2; event.reason = SC_NATIVE_UNOBSERVED;
  event.target_index = event.read_reason = event.win32_error = event.minhook_status = SC_INSTALL_UNKNOWN;
  state_.primary_failure = event; state_.phase = 2;
 }
 ReleaseSRWLockExclusive(&lock_);
}
sc_save_installation_snapshot Installation::inspect() const {
 AcquireSRWLockShared(&lock_); auto out = state_; ReleaseSRWLockShared(&lock_);
 out.size = sizeof(out); out.abi_version = 1; return out;
}
void attach_read(sc_install_event& event, engine::ReadResult read) {
 event.read_reason = read.reason; event.win32_error = read.error;
}
}
