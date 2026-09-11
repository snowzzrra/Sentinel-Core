#pragma once
#include "sentinel_save_installation.h"
#include "engine_observer.h"
#include "sentinel_native.h"
#include <windows.h>
namespace sentinel::save {
class Installation {
public:
 void initialize();
 sc_install_event begin(uint32_t stage, uint32_t group = 0, uint32_t index = SC_INSTALL_UNKNOWN,
                         uint32_t rva = 0, uint32_t offset = 0);
 void finish(sc_install_event event, uint32_t reason = 0,
             uint32_t minhook = SC_INSTALL_UNKNOWN, uint32_t win32 = SC_INSTALL_UNKNOWN);
 void startup(uint32_t observation); // 1 witnessed, 2 missed at import, 3 stopped unwitnessed
 sc_save_installation_snapshot inspect() const;
 template<class Operation> int hook(uint32_t stage, uint32_t group, uint32_t index, uint32_t rva, Operation operation) {
  auto event = begin(stage, group, index, rva);
  const int status = operation();
  finish(event, status ? SC_NATIVE_HOOK_FAILED : SC_NATIVE_NONE, static_cast<uint32_t>(status));
  return status;
 }
private:
 mutable SRWLOCK lock_ = SRWLOCK_INIT;
 sc_save_installation_snapshot state_{};
 bool mirror_ = false;
};
void attach_read(sc_install_event&, engine::ReadResult);
}
