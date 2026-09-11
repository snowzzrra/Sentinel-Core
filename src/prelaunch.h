#pragma once
#include "save_storage.h"
namespace sentinel::prelaunch {
storage::Result lease_key(std::string_view control, std::wstring& key);
const std::wstring& diagnostic_key(); // Local publication hash, never activation permission.
// Called only by the existing owner before qualified startup. A local file needs
// its live owner's matching, one-use kernel lease; stale publication is refused.
storage::Result resolve(const wchar_t* local_file, storage::Descriptor&, bool& requested);
storage::Result resolve(storage::Descriptor&, bool& requested);
}
