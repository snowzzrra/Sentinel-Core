#pragma once
#include "engine_observer.h"
#include <string>
#include <string_view>

namespace sentinel::expanded {
struct RecipeCheckpointCalls {
    void (*lock)() = nullptr;
    void (*unlock)() = nullptr;
    bool (*capture)(std::string&) = nullptr;
    bool (*restore)(std::string_view, std::string_view map) = nullptr;
};
bool install_recipe_checkpoint(const engine::Binding&, HANDLE stop_event, const RecipeCheckpointCalls&);
void stop_recipe_checkpoint();
bool recipe_checkpoint_enabled();
bool read_recipe_checkpoint(uintptr_t serializer, std::string_view map, uint32_t native_subtype);
#ifdef SC_NATIVE_TESTING
bool test_recipe_checkpoint();
#endif
}
