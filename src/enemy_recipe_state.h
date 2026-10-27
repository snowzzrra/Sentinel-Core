#pragma once
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>
#include <tuple>
#include <array>

namespace sentinel::expanded {
struct RecipeRequest {
    std::string owner, origin, label;
    uint64_t logical_mask = 0;
    uint32_t logical_units = 0, physical_units = 0;
    uint64_t maximum_emissions = 0;
    uint32_t credit_limit = 0, final_credit_units = 0;
    std::string parent_actor;
    std::string parent_request;
    bool operator==(const RecipeRequest& other) const {
        return owner == other.owner && origin == other.origin && label == other.label && logical_mask == other.logical_mask &&
            logical_units == other.logical_units && physical_units == other.physical_units &&
            maximum_emissions == other.maximum_emissions && credit_limit == other.credit_limit &&
            final_credit_units == other.final_credit_units && parent_actor == other.parent_actor && parent_request == other.parent_request;
    }
};
struct RecipeEmission {
    std::string actor;
    uint64_t ordinal = 0;
};
enum class RecipeActorState : uint8_t { alive, dead, removed };
struct RecipeMembership {
    std::string owner, label;
    bool operator==(const RecipeMembership& other) const { return owner == other.owner && label == other.label; }
};
struct RecipeActor {
    uint64_t ordinal = 0;
    std::vector<RecipeMembership> memberships;
    std::string native_name;
};
struct RecipeProgress {
    RecipeRequest request;
    uint64_t emitted = 0, dead = 0, removed = 0;
    bool closed = false, reserved = false, empty_completed = false;
    std::map<std::string, RecipeActor> actors;
};
struct RecipeCredit { uint64_t numerator = 0; uint32_t denominator = 1; };
struct RecipeHealth { RecipeCredit credit; float initial = 0, current = 0; };
struct RecipeCreditScope {
    std::string request, owner, label;
    uint32_t units = 0;
    bool operator<(const RecipeCreditScope& other) const {
        return std::tie(request, owner, label, units) < std::tie(other.request, other.owner, other.label, other.units);
    }
};
using RecipeBaseline = std::map<RecipeCreditScope, uint64_t>;
struct RecipeWatch {
    uint64_t mask = 0;
    std::string owner, label;
    RecipeBaseline baseline;
};
bool credit_at_least(const std::vector<RecipeCredit>&, uint64_t threshold);
bool credit_at_most(const std::vector<RecipeCredit>&, uint64_t threshold);
std::optional<bool> health_at_most(const std::vector<RecipeHealth>&, float threshold);

// this state records recipe progress; native commands decide when to emit or cancel.
class RecipeState final {
public:
    bool reset(std::string plan, std::string map);
    bool request(std::string key, const RecipeRequest&);
    std::optional<RecipeEmission> reserve(std::string_view key);
    bool release(std::string_view key);
    bool created(std::string_view key, const RecipeEmission&, std::string native_name = {});
    bool rebind_actor(std::string_view actor, std::string native_name);
    bool parent_pending(std::string_view actor) const;
    bool complete_empty(std::string_view key, bool reactivated = false);
    bool join_empty(std::string_view key, std::string owner, std::string label);
    bool pending_empty(std::string_view owner, uint64_t mask, std::string_view label) const;
    bool cancel(std::string_view key);
    bool native_quota(std::string_view key, uint64_t maximum_emissions);
    bool join_actor(std::string_view actor, std::string owner, std::string label);
    bool detach_actor(std::string_view actor, std::string_view owner, std::string_view label);
    std::vector<RecipeMembership> request_memberships(std::string_view key) const;
    bool actor_changed(std::string_view actor, RecipeActorState);
    RecipeBaseline death_baseline(std::string_view owner, uint64_t mask, std::string_view label) const;
    std::vector<RecipeCredit> deaths(std::string_view owner, uint64_t mask, std::string_view label,
                                    const RecipeBaseline& baseline = {}, bool empty_only = false) const;
    std::vector<RecipeCredit> alive(std::string_view owner, uint64_t mask, std::string_view label) const;
    std::vector<RecipeCredit> outstanding(std::string_view owner, uint64_t mask, std::string_view label) const;
    bool watch(std::string key, std::string owner, uint64_t mask, std::string label);
    bool end_watch(std::string_view key);
    std::optional<std::vector<RecipeCredit>> watched_deaths(std::string_view key) const;
    const std::map<std::string, RecipeWatch>& watches() const { return watches_; }
    const std::map<std::string, RecipeProgress>& requests() const { return requests_; }
    const std::string& plan() const { return plan_; }
    const std::string& map() const { return map_; }
    bool capture(std::string&) const;
    bool capture(std::string&, std::string_view request_prefix) const;
    bool capture(std::string&, const std::vector<std::string_view>& request_prefixes) const;
    bool restore(std::string_view bytes, std::string_view expected_plan, std::string_view expected_map);
private:
    std::string plan_, map_;
    std::map<std::string, RecipeProgress> requests_;
    std::map<std::string, RecipeWatch> watches_;
    RecipeBaseline credits_;
    std::pair<RecipeProgress*, RecipeActor*> actor_state(std::string_view actor);
};
struct RecipeNativeCheckpoint {
    std::string plan, map, state;
    std::map<std::string, std::array<float, 12>> shows;
    struct Action { uint64_t units = 0, sent = 0; uint32_t denominator = 0; };
    std::map<std::pair<std::string, std::string>, Action> actions;
    struct Source {
        uint64_t monster = 0, mask = 0;
        float offset = 0;
        std::array<float, 3> position{};
        std::string origin, label, template_name;
    };
    struct Summon {
        std::string key, origin;
        uint64_t body_offset = 0;
        bool completed = false;
        std::vector<Source> sources;
        std::vector<RecipeMembership> memberships;
    };
    struct Component {
        std::string parent, definition;
        std::string template_name;
        uint64_t mask = 0;
        uint8_t phase = 0;
        float playback = 1;
        bool paused = false, template_mode = false;
        uint64_t epoch = 0;
        bool completed = false;
        std::vector<Summon> requests;
        std::map<std::string, std::set<std::string>> notifications;
    };
    struct Child {
        std::string parent, body;
        std::array<float, 12> pose{};
        std::array<float, 4> health{};
    };
    uint64_t next_request = 0;
    std::vector<Component> summons;
    std::map<std::string, Child> children;
    struct Identity {
        std::string request, body;
        uint64_t ordinal = 0, units = 0;
        uint32_t denominator = 0;
    };
    std::map<std::string, Identity> identities;
    bool capture(std::string&) const;
    bool restore(std::string_view bytes, std::string_view expected_map);
};
}
