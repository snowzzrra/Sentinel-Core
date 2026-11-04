#include "build_descriptor.h"

namespace sentinel::builds {
native::Target resolve(uintptr_t base, native::Target relative) {
    uintptr_t address = 0;
    if (engine::add(base, relative.address, relative.bytes.size(), address)) relative.address = address;
    else relative.address = 0;
    return relative;
}
}
