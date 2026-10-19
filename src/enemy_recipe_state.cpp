#include "enemy_recipe_state.h"
#include <algorithm>
#include <limits>
#include <numeric>
#include <set>
#include <cmath>
#include <charconv>
#include <cstring>

namespace sentinel::expanded {
namespace {
constexpr size_t max_bytes = 8 * 1024 * 1024;
constexpr uint32_t max_requests = 100000, max_actors = 1000000;
bool identifier(std::string_view value) {
    return !value.empty() && value.size() <= 191 &&
        value.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_/-.") == value.npos;
}
bool valid(const RecipeRequest& value) {
    return identifier(value.owner) && identifier(value.origin) && value.label.size() <= 1024 && value.label.find('\0') == value.label.npos &&
        value.logical_mask && value.logical_mask < (uint64_t{1} << 44) &&
        !(value.logical_mask & (value.logical_mask - 1)) &&
        value.logical_units && value.logical_units <= INT32_MAX && value.physical_units <= INT32_MAX &&
        value.maximum_emissions <= INT32_MAX && (value.physical_units || !value.maximum_emissions) &&
        (value.parent_actor.empty() || identifier(value.parent_actor)) &&
        (value.parent_request.empty() || identifier(value.parent_request)) &&
        ((!value.credit_limit && !value.final_credit_units) ||
         (value.physical_units && value.credit_limit && value.credit_limit <= INT32_MAX &&
          value.final_credit_units && value.final_credit_units <= value.logical_units));
}
bool valid_parent(std::string_view actor, const std::map<std::string, RecipeProgress>& requests) {
    const auto separator = actor.rfind('/');
    if (separator == actor.npos) return false;
    const auto request = requests.find(std::string(actor.substr(0, separator)));
    if (request == requests.end()) return false;
    const auto ordinal = actor.substr(separator + 1);
    uint64_t number = 0;
    const auto parsed = std::from_chars(ordinal.data(), ordinal.data() + ordinal.size(), number);
    return parsed.ec == std::errc{} && parsed.ptr == ordinal.data() + ordinal.size() &&
        std::to_string(number) == ordinal && (number < request->second.emitted ||
            (!number && request->second.request.physical_units));
}
uint32_t credit_units(const RecipeRequest& request, uint64_t ordinal) {
    return request.credit_limit && ordinal == request.credit_limit - 1 ? request.final_credit_units : request.logical_units;
}
uint64_t credit_units(const RecipeRequest& request, uint64_t first, uint64_t end) {
    auto units = (end - first) * request.logical_units;
    if (request.credit_limit && first < request.credit_limit && end >= request.credit_limit)
        units -= request.logical_units - request.final_credit_units;
    return units;
}
bool selects(std::string_view member_owner, std::string_view member_label, std::string_view owner, std::string_view label) {
    return member_owner == owner && (label.empty() || member_label == label);
}
bool selects(const RecipeRequest& request, uint64_t mask) {
    if (mask & (uint64_t{1} << 44)) mask |= (uint64_t{1} << 44) - 1;
    return (request.logical_mask & mask) != 0;
}
bool valid_member(const RecipeMembership& value) {
    return identifier(value.owner) && value.label.size() <= 1024 && value.label.find('\0') == value.label.npos;
}
struct Integer {
    std::vector<uint32_t> words{0};
    explicit Integer(uint32_t value = 0) : words{value} {}
    void multiply(uint32_t value) {
        uint64_t carry = 0;
        for (auto& word : words) {
            const auto product = uint64_t{word} * value + carry;
            word = static_cast<uint32_t>(product); carry = product >> 32;
        }
        if (carry) words.push_back(static_cast<uint32_t>(carry));
        trim();
    }
    void add(const Integer& other) {
        words.resize((std::max)(words.size(), other.words.size()), 0);
        uint64_t carry = 0;
        for (size_t i = 0; i < words.size(); ++i) {
            const auto sum = uint64_t{words[i]} + (i < other.words.size() ? other.words[i] : 0) + carry;
            words[i] = static_cast<uint32_t>(sum); carry = sum >> 32;
        }
        if (carry) words.push_back(static_cast<uint32_t>(carry));
    }
    Integer times(uint64_t value) const {
        auto low = *this, high = *this;
        low.multiply(static_cast<uint32_t>(value)); high.multiply(static_cast<uint32_t>(value >> 32));
        high.words.insert(high.words.begin(), 0); low.add(high); low.trim(); return low;
    }
    Integer times(const Integer& value) const {
        Integer result;
        for (size_t i = value.words.size(); i-- > 0;) {
            result.words.insert(result.words.begin(), 0);
            result.add(times(value.words[i]));
        }
        result.trim(); return result;
    }
    void shift(unsigned bits) {
        words.insert(words.begin(), bits / 32, 0);
        multiply(uint32_t{1} << (bits % 32));
    }
    void trim() { while (words.size() > 1 && words.back() == 0) words.pop_back(); }
    int compare(const Integer& other) const {
        if (words.size() != other.words.size()) return words.size() < other.words.size() ? -1 : 1;
        for (size_t i = words.size(); i-- > 0;)
            if (words[i] != other.words[i]) return words[i] < other.words[i] ? -1 : 1;
        return 0;
    }
};
int compare_credit(const std::vector<RecipeCredit>& values, uint64_t threshold) {
    std::map<uint32_t, Integer> groups;
    for (const auto& value : values) {
        if (!value.denominator) return -2;
        if (!value.numerator) continue;
        const auto divisor = std::gcd(value.numerator, uint64_t{value.denominator});
        const auto divided = static_cast<uint32_t>(value.denominator / divisor);
        const auto amount = value.numerator / divisor;
        Integer term;
        term.words = {static_cast<uint32_t>(amount), static_cast<uint32_t>(amount >> 32)};
        term.trim(); groups[divided].add(term);
    }
    Integer numerator, denominator(1);
    for (const auto& [divided, amount] : groups) {
        numerator.multiply(divided);
        numerator.add(denominator.times(amount));
        denominator.multiply(divided);
        if (numerator.compare(denominator.times(threshold)) > 0) return 1;
    }
    return numerator.compare(denominator.times(threshold));
}
struct BinaryFloat { uint32_t mantissa; int exponent; };
BinaryFloat binary_float(float value) {
    int exponent = 0;
    const auto fraction = std::frexp(value, &exponent);
    return {static_cast<uint32_t>(std::ldexp(fraction, 24)), value == 0 ? 0 : exponent - 24};
}
Integer health_integer(float value, int exponent, uint64_t weight) {
    const auto parts = binary_float((std::max)(0.0f, value));
    auto result = Integer(parts.mantissa).times(weight);
    result.shift(static_cast<unsigned>(parts.exponent - exponent)); return result;
}
void integer(std::string& out, uint64_t value, unsigned bytes) {
    for (unsigned i = 0; i < bytes; ++i) out.push_back(static_cast<char>(value >> (i * 8)));
}
void text(std::string& out, std::string_view value) {
    integer(out, value.size(), 4); out.append(value);
}
void credit_scope(std::string& out, const RecipeCreditScope& scope) {
    text(out, scope.request); text(out, scope.owner); text(out, scope.label); integer(out, scope.units, 4);
}
struct Reader {
    std::string_view bytes;
    bool integer(uint64_t& value, unsigned size) {
        if (bytes.size() < size) return false;
        value = 0;
        for (unsigned i = 0; i < size; ++i) value |= uint64_t{static_cast<uint8_t>(bytes[i])} << (8 * i);
        bytes.remove_prefix(size); return true;
    }
    bool text(std::string& out, size_t maximum) {
        uint64_t size = 0;
        if (!integer(size, 4) || size > maximum || size > bytes.size()) return false;
        out.assign(bytes.substr(0, static_cast<size_t>(size))); bytes.remove_prefix(static_cast<size_t>(size)); return true;
    }
    bool credit_scope(RecipeCreditScope& scope) {
        uint64_t units = 0;
        if (!text(scope.request, 191) || !text(scope.owner, 191) || !text(scope.label, 1024) ||
            !integer(units, 4) || !valid_member({scope.owner, scope.label})) return false;
        scope.units = static_cast<uint32_t>(units); return true;
    }
};
template<size_t N> void floats(std::string& out, const std::array<float, N>& values) {
    for (const auto value : values) {
        uint32_t bits; std::memcpy(&bits, &value, sizeof(bits)); integer(out, bits, 4);
    }
}
template<size_t N> bool floats(Reader& in, std::array<float, N>& values) {
    for (auto& value : values) {
        uint64_t number = 0;
        if (!in.integer(number, 4)) return false;
        const auto bits = static_cast<uint32_t>(number); std::memcpy(&value, &bits, sizeof(bits));
        if (!std::isfinite(value)) return false;
    }
    return true;
}
void summon_source(std::string& out, const RecipeNativeCheckpoint::Source& source) {
    integer(out, source.monster, 8); integer(out, source.mask, 8);
    floats(out, std::array<float, 1>{source.offset}); floats(out, source.position);
    text(out, source.origin); text(out, source.label); text(out, source.template_name);
}
bool summon_source(Reader& in, RecipeNativeCheckpoint::Source& source) {
    std::array<float, 1> offset{};
    if (!in.integer(source.monster, 8) || !in.integer(source.mask, 8) || !source.mask ||
        !floats(in, offset) || !floats(in, source.position) || !in.text(source.origin, 20) || source.origin.size() != 20 ||
        source.origin.find_first_not_of("0123456789abcdef") != source.origin.npos ||
        !in.text(source.label, 1024) || source.label.find('\0') != source.label.npos ||
        !in.text(source.template_name, 511) || source.template_name.find('\0') != source.template_name.npos) return false;
    source.offset = offset[0]; return true;
}
}
bool credit_at_least(const std::vector<RecipeCredit>& value, uint64_t threshold) {
    return compare_credit(value, threshold) >= 0;
}
bool credit_at_most(const std::vector<RecipeCredit>& value, uint64_t threshold) {
    const auto compared = compare_credit(value, threshold); return compared != -2 && compared <= 0;
}
std::optional<bool> health_at_most(const std::vector<RecipeHealth>& values, float threshold) {
    if (!std::isfinite(threshold) || threshold < 0) return {};
    int exponent = 0;
    for (const auto& value : values) {
        if (!value.credit.denominator || !std::isfinite(value.initial) || value.initial < 0 || !std::isfinite(value.current)) return {};
        exponent = (std::min)(exponent, binary_float(value.initial).exponent);
        exponent = (std::min)(exponent, binary_float((std::max)(0.0f, value.current)).exponent);
    }
    struct Mass { Integer initial, current; };
    std::map<uint32_t, Mass> groups;
    for (const auto& value : values) {
        auto& group = groups[value.credit.denominator];
        group.initial.add(health_integer(value.initial, exponent, value.credit.numerator));
        group.current.add(health_integer(value.current, exponent, value.credit.numerator));
    }
    Integer initial, current, denominator(1);
    for (const auto& [divisor, mass] : groups) {
        initial.multiply(divisor); initial.add(denominator.times(mass.initial));
        current.multiply(divisor); current.add(denominator.times(mass.current));
        denominator.multiply(divisor);
    }
    if (initial.compare(Integer{}) == 0) return {};
    const auto limit = binary_float(threshold);
    initial.multiply(limit.mantissa);
    if (limit.exponent < 0) current.shift(static_cast<unsigned>(-limit.exponent));
    else initial.shift(static_cast<unsigned>(limit.exponent));
    return current.compare(initial) <= 0;
}
bool RecipeState::reset(std::string plan, std::string map) {
    if (!identifier(plan) || !identifier(map)) return false;
    plan_ = std::move(plan); map_ = std::move(map); requests_.clear(); watches_.clear(); credits_.clear(); return true;
}
bool RecipeState::request(std::string key, const RecipeRequest& spec) {
    if (plan_.empty() || !identifier(key) || !valid(spec)) return false;
    const auto found = requests_.find(key);
    if (found != requests_.end()) return found->second.request == spec;
    if (!spec.parent_actor.empty() && !valid_parent(spec.parent_actor, requests_)) return false;
    if (!spec.parent_request.empty() && (!requests_.count(spec.parent_request) ||
        (!spec.parent_actor.empty() && spec.parent_actor.substr(0, spec.parent_actor.rfind('/')) != spec.parent_request))) return false;
    if (requests_.size() >= max_requests) return false;
    RecipeProgress value; value.request = spec; requests_.emplace(std::move(key), std::move(value)); return true;
}
std::optional<RecipeEmission> RecipeState::reserve(std::string_view key) {
    const auto found = requests_.find(std::string(key));
    if (found == requests_.end()) return {};
    auto& value = found->second;
    if (value.closed || value.reserved || !value.request.physical_units || value.emitted >= UINT64_MAX / value.request.logical_units ||
        (value.request.maximum_emissions && value.emitted >= value.request.maximum_emissions)) return {};
    RecipeEmission emission{found->first + "/" + std::to_string(value.emitted), value.emitted};
    value.reserved = true;
    return emission;
}
bool RecipeState::release(std::string_view key) {
    const auto found = requests_.find(std::string(key));
    if (found == requests_.end() || !found->second.reserved) return false;
    found->second.reserved = false; return true;
}
bool RecipeState::parent_pending(std::string_view actor) const {
    if (actor.empty()) return false;
    const auto separator = actor.rfind('/');
    if (separator == actor.npos) return false;
    const auto request = requests_.find(std::string(actor.substr(0, separator)));
    return request != requests_.end() && !request->second.closed && !request->second.emitted;
}
bool RecipeState::complete_empty(std::string_view key, bool reactivated) {
    const auto found = requests_.find(std::string(key));
    if (found == requests_.end()) return false;
    auto& value = found->second;
    if (value.empty_completed || (value.closed && !reactivated) || value.request.physical_units) return false;
    credits_.emplace(RecipeCreditScope{found->first, value.request.owner, value.request.label}, 1);
    for (auto& [scope, count] : credits_) if (scope.request == key) count = 1;
    value.closed = true; value.empty_completed = true; return true;
}
bool RecipeState::join_empty(std::string_view key, std::string owner, std::string label) {
    const auto found = requests_.find(std::string(key));
    if (found == requests_.end() || found->second.request.physical_units || !valid_member({owner, label})) return false;
    for (auto previous = credits_.begin(); previous != credits_.end(); ++previous) {
        if (previous->first.request != key || previous->first.owner != owner || previous->first.label == label) continue;
        if (found->second.empty_completed) return true;
        credits_.erase(previous); break;
    }
    const RecipeCreditScope scope{found->first, std::move(owner), std::move(label)};
    if (!credits_.count(scope) && credits_.size() >= max_requests) return false;
    credits_.emplace(scope, found->second.empty_completed ? 1 : 0); return true;
}
bool RecipeState::pending_empty(std::string_view owner, uint64_t mask, std::string_view label) const {
    for (const auto& [scope, count] : credits_) {
        (void)count;
        const auto& progress = requests_.at(scope.request);
        if (!progress.request.physical_units && !progress.closed && selects(progress.request, mask) &&
            selects(scope.owner, scope.label, owner, label)) return true;
    }
    return false;
}
bool RecipeState::created(std::string_view key, const RecipeEmission& emission, std::string native_name) {
    if (native_name.size() > 511 || native_name.find('\0') != native_name.npos) return false;
    const auto found = requests_.find(std::string(key));
    if (found == requests_.end()) return false;
    auto& value = found->second;
    if (!value.reserved || emission.ordinal != value.emitted ||
        emission.actor != found->first + "/" + std::to_string(value.emitted)) return false;
    value.actors.emplace(emission.actor, RecipeActor{emission.ordinal, {{value.request.owner, value.request.label}}, std::move(native_name)});
    ++value.emitted; value.reserved = false; return true;
}
bool RecipeState::cancel(std::string_view key) {
    const auto found = requests_.find(std::string(key));
    if (found == requests_.end()) return false;
    found->second.closed = true; return true;
}
bool RecipeState::native_quota(std::string_view key, uint64_t maximum_emissions) {
    const auto found = requests_.find(std::string(key));
    if (found == requests_.end() || found->second.closed || maximum_emissions > INT32_MAX) return false;
    found->second.request.maximum_emissions = maximum_emissions; return true;
}
std::pair<RecipeProgress*, RecipeActor*> RecipeState::actor_state(std::string_view actor) {
    const auto separator = actor.rfind('/');
    if (separator == actor.npos) return {};
    const auto request = requests_.find(std::string(actor.substr(0, separator)));
    if (request == requests_.end()) return {};
    const auto found = request->second.actors.find(std::string(actor));
    return found == request->second.actors.end() ? std::pair<RecipeProgress*, RecipeActor*>{} :
        std::pair<RecipeProgress*, RecipeActor*>{&request->second, &found->second};
}
bool RecipeState::join_actor(std::string_view actor, std::string owner, std::string label) {
    const auto found = actor_state(actor);
    RecipeMembership member{std::move(owner), std::move(label)};
    if (!found.second || !valid_member(member)) return false;
    auto& members = found.second->memberships;
    if (std::find(members.begin(), members.end(), member) != members.end()) return true;
    if (members.size() >= max_requests) return false;
    members.push_back(std::move(member)); return true;
}
bool RecipeState::rebind_actor(std::string_view actor, std::string native_name) {
    const auto found = actor_state(actor);
    if (!found.second || native_name.empty() || native_name.size() > 511 || native_name.find('\0') != native_name.npos) return false;
    found.second->native_name = std::move(native_name); return true;
}
std::vector<RecipeMembership> RecipeState::request_memberships(std::string_view key) const {
    const auto found = requests_.find(std::string(key));
    if (found == requests_.end()) return {};
    std::vector<RecipeMembership> result{{found->second.request.owner, found->second.request.label}};
    const auto append = [&](RecipeMembership member) {
        if (std::find(result.begin(), result.end(), member) == result.end()) result.push_back(std::move(member));
    };
    for (const auto& [role, actor] : found->second.actors) { (void)role; for (const auto& member : actor.memberships) append(member); }
    for (const auto& [scope, count] : credits_) { (void)count; if (scope.request == key) append({scope.owner, scope.label}); }
    return result;
}
bool RecipeState::detach_actor(std::string_view actor, std::string_view owner, std::string_view label) {
    const auto found = actor_state(actor);
    if (!found.second) return false;
    auto& members = found.second->memberships;
    const auto at = std::find(members.begin(), members.end(), RecipeMembership{std::string(owner), std::string(label)});
    if (at == members.end()) return false;
    members.erase(at); return true;
}
bool RecipeState::actor_changed(std::string_view actor, RecipeActorState state) {
    if (state == RecipeActorState::alive || state > RecipeActorState::removed) return false;
    const auto separator = actor.rfind('/');
    if (separator == actor.npos) return false;
    const auto request = requests_.find(std::string(actor.substr(0, separator)));
    if (request == requests_.end()) return false;
    const auto found = request->second.actors.find(std::string(actor));
    if (found == request->second.actors.end()) return false;
    if (state == RecipeActorState::dead) {
        for (const auto& member : found->second.memberships)
            ++credits_[RecipeCreditScope{request->first, member.owner, member.label,
                credit_units(request->second.request, found->second.ordinal)}];
        ++request->second.dead;
    }
    else ++request->second.removed;
    request->second.actors.erase(found); return true;
}
RecipeBaseline RecipeState::death_baseline(std::string_view owner, uint64_t mask, std::string_view label) const {
    RecipeBaseline result;
    for (const auto& [scope, dead] : credits_)
        if (selects(requests_.at(scope.request).request, mask) && selects(scope.owner, scope.label, owner, label))
            result.emplace(scope, dead);
    return result;
}
std::vector<RecipeCredit> RecipeState::deaths(std::string_view owner, uint64_t mask, std::string_view label,
                                            const RecipeBaseline& baseline, bool empty_only) const {
    std::vector<RecipeCredit> result;
    for (const auto& [scope, dead] : credits_) {
        const auto& value = requests_.at(scope.request);
        if (empty_only && value.request.physical_units) continue;
        if (!selects(value.request, mask) || !selects(scope.owner, scope.label, owner, label)) continue;
        const auto before = baseline.find(scope), end = baseline.end();
        const auto count = dead - (std::min)(dead, before == end ? uint64_t{0} : before->second);
        result.push_back({count * (scope.units ? scope.units : value.request.logical_units),
                          (std::max)(uint32_t{1}, value.request.physical_units)});
    }
    return result;
}
std::vector<RecipeCredit> RecipeState::alive(std::string_view owner, uint64_t mask, std::string_view label) const {
    std::vector<RecipeCredit> result;
    for (const auto& [key, value] : requests_) {
        (void)key;
        if (!selects(value.request, mask) || !value.request.physical_units) continue;
        uint64_t units = 0;
        for (const auto& [actor, state] : value.actors) {
            (void)actor;
            if (std::any_of(state.memberships.begin(), state.memberships.end(),
                [&](const auto& member) { return selects(member.owner, member.label, owner, label); }))
                units += credit_units(value.request, state.ordinal);
        }
        result.push_back({units, value.request.physical_units});
    }
    return result;
}
std::vector<RecipeCredit> RecipeState::outstanding(std::string_view owner, uint64_t mask, std::string_view label) const {
    auto result = alive(owner, mask, label);
    for (const auto& [key, value] : requests_) {
        (void)key;
        const auto& request = value.request;
        if (!selects(request, mask) || !selects(request.owner, request.label, owner, label) || value.closed ||
            request.maximum_emissions <= value.emitted) continue;
        result.push_back({credit_units(request, value.emitted, request.maximum_emissions), request.physical_units});
    }
    return result;
}
bool RecipeState::watch(std::string key, std::string owner, uint64_t mask, std::string label) {
    if (mask & (uint64_t{1} << 44)) mask |= (uint64_t{1} << 44) - 1;
    mask &= (uint64_t{1} << 44) - 1;
    if (plan_.empty() || !identifier(key) || !mask || !valid_member({owner, label})) return false;
    const auto found = watches_.find(key);
    if (found != watches_.end()) return found->second.mask == mask && found->second.owner == owner && found->second.label == label;
    if (watches_.size() >= max_requests) return false;
    const auto baseline = death_baseline(owner, mask, label);
    watches_.emplace(std::move(key), RecipeWatch{mask, std::move(owner), std::move(label), baseline}); return true;
}
bool RecipeState::end_watch(std::string_view key) { return watches_.erase(std::string(key)) != 0; }
std::optional<std::vector<RecipeCredit>> RecipeState::watched_deaths(std::string_view key) const {
    const auto found = watches_.find(std::string(key));
    if (found == watches_.end()) return {};
    return deaths(found->second.owner, found->second.mask, found->second.label, found->second.baseline);
}
bool RecipeNativeCheckpoint::capture(std::string& out) const {
    out.clear();
    RecipeState checked;
    if (!identifier(plan) || !identifier(map) || !checked.restore(state, plan, map) ||
        shows.size() > max_actors || actions.size() > max_requests || summons.size() > max_requests ||
        children.size() > max_actors || identities.size() > max_actors) return false;
    std::string bytes("EAPN\2", 5);
    text(bytes, plan); text(bytes, map); text(bytes, state); integer(bytes, shows.size(), 4);
    for (const auto& [name, pose] : shows) {
        if (name.empty() || name.size() > 511 || name.find('\0') != name.npos) return false;
        text(bytes, name);
        for (const auto value : pose) {
            if (!std::isfinite(value)) return false;
            uint32_t bits; std::memcpy(&bits, &value, sizeof(bits)); integer(bytes, bits, 4);
        }
        if (bytes.size() > max_bytes) return false;
    }
    integer(bytes, actions.size(), 4);
    for (const auto& [identity, credit] : actions) {
        if (!identifier(identity.first) || identity.second.empty() || identity.second.size() > 511 ||
            identity.second.find('\0') != identity.second.npos || !credit.denominator || credit.sent > credit.units / credit.denominator)
            return false;
        text(bytes, identity.first); text(bytes, identity.second);
        integer(bytes, credit.units, 8); integer(bytes, credit.sent, 8); integer(bytes, credit.denominator, 4);
        if (bytes.size() > max_bytes) return false;
    }
    integer(bytes, next_request, 8); integer(bytes, summons.size(), 4);
    for (const auto& component : summons) {
        text(bytes, component.parent); text(bytes, component.definition);
        text(bytes, component.template_name); integer(bytes, component.mask, 8); integer(bytes, component.phase, 1);
        floats(bytes, std::array<float, 1>{component.playback}); integer(bytes, component.paused, 1); integer(bytes, component.template_mode, 1);
        integer(bytes, component.epoch, 8); integer(bytes, component.completed, 1); integer(bytes, component.requests.size(), 4);
        for (const auto& request : component.requests) {
            text(bytes, request.key); text(bytes, request.origin); integer(bytes, request.body_offset, 8);
            integer(bytes, request.completed, 1); integer(bytes, request.sources.size(), 4);
            for (const auto& source : request.sources) summon_source(bytes, source);
            integer(bytes, request.memberships.size(), 4);
            for (const auto& member : request.memberships) { text(bytes, member.owner); text(bytes, member.label); }
        }
        integer(bytes, component.notifications.size(), 4);
        for (const auto& [target, dependencies] : component.notifications) {
            text(bytes, target); integer(bytes, dependencies.size(), 4);
            for (const auto& key : dependencies) text(bytes, key);
        }
        if (bytes.size() > max_bytes) return false;
    }
    integer(bytes, children.size(), 4);
    for (const auto& [role, child] : children) {
        text(bytes, role); text(bytes, child.parent); text(bytes, child.body);
        floats(bytes, child.pose); floats(bytes, child.health);
        if (bytes.size() > max_bytes) return false;
    }
    integer(bytes, identities.size(), 4);
    for (const auto& [name, identity] : identities) {
        text(bytes, name); text(bytes, identity.request); text(bytes, identity.body);
        integer(bytes, identity.ordinal, 8); integer(bytes, identity.units, 8); integer(bytes, identity.denominator, 4);
    }
    RecipeNativeCheckpoint roundtrip;
    if (bytes.size() > max_bytes || !roundtrip.restore(bytes, map)) return false;
    out = std::move(bytes); return true;
}
bool RecipeNativeCheckpoint::restore(std::string_view bytes, std::string_view expected_map) {
    if (bytes.size() > max_bytes || bytes.substr(0, 4) != "EAPN" || bytes.size() < 5 || (bytes[4] != 1 && bytes[4] != 2)) return false;
    const bool autonomous = bytes[4] == 2;
    Reader in{bytes.substr(5)}; RecipeNativeCheckpoint restored;
    RecipeState checked;
    uint64_t count = 0;
    if (!in.text(restored.plan, 191) || !in.text(restored.map, 191) || restored.map != expected_map ||
        !in.text(restored.state, max_bytes) || !checked.restore(restored.state, restored.plan, restored.map) ||
        !in.integer(count, 4) || count > max_actors) return false;
    for (uint64_t index = 0; index < count; ++index) {
        std::string name; std::array<float, 12> pose;
        if (!in.text(name, 511) || name.empty() || name.find('\0') != name.npos) return false;
        for (auto& value : pose) {
            uint64_t number = 0;
            if (!in.integer(number, 4)) return false;
            const auto bits = static_cast<uint32_t>(number); std::memcpy(&value, &bits, sizeof(bits));
            if (!std::isfinite(value)) return false;
        }
        if (!restored.shows.emplace(name, pose).second) return false;
    }
    if (!in.integer(count, 4) || count > max_requests) return false;
    for (uint64_t index = 0; index < count; ++index) {
        std::pair<std::string, std::string> identity; Action credit; uint64_t denominator = 0;
        if (!in.text(identity.first, 191) || !identifier(identity.first) || !in.text(identity.second, 511) || identity.second.empty() ||
            identity.second.find('\0') != identity.second.npos || !in.integer(credit.units, 8) || !in.integer(credit.sent, 8) ||
            !in.integer(denominator, 4) || !denominator || credit.sent > credit.units / denominator) return false;
        credit.denominator = static_cast<uint32_t>(denominator);
        if (!restored.actions.emplace(identity, credit).second) return false;
    }
    if (autonomous) {
        uint64_t size = 0, flag = 0;
        std::set<std::string> parents, requests;
        const auto name = [&in](std::string& value) { return in.text(value, 511) && !value.empty() && value.find('\0') == value.npos; };
        const auto autonomous_key = [](const std::string& key) { return key.rfind("summon/", 0) == 0 || key.rfind("direct/", 0) == 0; };
        if (!in.integer(restored.next_request, 8) || !in.integer(count, 4) || count > max_requests) return false;
        for (uint64_t i = 0; i < count; ++i) {
            Component component;
            if (!name(component.parent) || !parents.insert(component.parent).second || !name(component.definition) ||
                !in.text(component.template_name, 511) || component.template_name.find('\0') != component.template_name.npos ||
                !in.integer(component.mask, 8) || !in.integer(flag, 1) || flag > 3) return false;
            component.phase = static_cast<uint8_t>(flag);
            std::array<float, 1> playback{};
            if (!floats(in, playback) || playback[0] < 0 || !in.integer(flag, 1) || flag > 1) return false;
            component.playback = playback[0]; component.paused = flag != 0;
            if (!in.integer(flag, 1) || flag > 1) return false;
            component.template_mode = flag != 0;
            if (!in.integer(component.epoch, 8) || !in.integer(flag, 1) || flag > 1 || !in.integer(size, 4) || size > max_requests) return false;
            component.completed = flag != 0;
            for (uint64_t j = 0; j < size; ++j) {
                Summon request; uint64_t sources = 0;
                if (!in.text(request.key, 191) || !autonomous_key(request.key) || !requests.insert(request.key).second ||
                    !in.text(request.origin, 20) || !in.integer(request.body_offset, 8) || !in.integer(flag, 1) || flag > 1 ||
                    !in.integer(sources, 4) || !sources || sources > max_requests) return false;
                const auto progress = checked.requests().find(request.key);
                if (progress == checked.requests().end() || progress->second.request.origin != request.origin) return false;
                request.completed = flag != 0;
                for (uint64_t k = 0; k < sources; ++k) {
                    Source source;
                    if (!summon_source(in, source) || source.origin != request.origin || source.label != progress->second.request.label) return false;
                    request.sources.push_back(std::move(source));
                }
                uint64_t members = 0;
                if (!in.integer(members, 4) || members > max_requests) return false;
                for (uint64_t k = 0; k < members; ++k) {
                    RecipeMembership member;
                    if (!name(member.owner) || !in.text(member.label, 1024) || member.label.find('\0') != member.label.npos ||
                        std::find(request.memberships.begin(), request.memberships.end(), member) != request.memberships.end()) return false;
                    request.memberships.push_back(std::move(member));
                }
                component.requests.push_back(std::move(request));
            }
            if (!in.integer(size, 4) || size > max_requests) return false;
            for (uint64_t j = 0; j < size; ++j) {
                std::string target; uint64_t dependencies = 0;
                if (!name(target) || component.notifications.count(target) || !in.integer(dependencies, 4) || dependencies > max_requests) return false;
                auto& values = component.notifications[target];
                for (uint64_t k = 0; k < dependencies; ++k) {
                    std::string key;
                    if (!in.text(key, 191) || !autonomous_key(key) || !checked.requests().count(key) || !values.insert(key).second) return false;
                }
            }
            restored.summons.push_back(std::move(component));
        }
        if (!in.integer(count, 4) || count > max_actors) return false;
        for (uint64_t i = 0; i < count; ++i) {
            std::string role; Child child;
            if (!in.text(role, 191) || !in.text(child.parent, 511) || child.parent.find('\0') != child.parent.npos ||
                (!child.parent.empty() && !parents.count(child.parent)) || !name(child.body) || !floats(in, child.pose) || !floats(in, child.health) ||
                std::any_of(child.health.begin(), child.health.end(), [](float value) { return value < 0; })) return false;
            const auto progress = checked.requests().find(role.substr(0, role.rfind('/')));
            if (progress == checked.requests().end() || !autonomous_key(role) || !progress->second.actors.count(role) ||
                !restored.children.emplace(role, std::move(child)).second) return false;
        }
        for (const auto& [key, progress] : checked.requests()) {
            if (!autonomous_key(key)) continue;
            if (!progress.closed && !requests.count(key)) return false;
            for (const auto& [role, actor] : progress.actors) {
                (void)actor;
                if (!restored.children.count(role)) return false;
            }
        }
        if (!in.integer(count, 4) || count > max_actors) return false;
        for (uint64_t i = 0; i < count; ++i) {
            std::string native_name; Identity identity; uint64_t denominator = 0;
            if (!name(native_name) || !in.text(identity.request, 191) || !identifier(identity.request) || !name(identity.body) ||
                !in.integer(identity.ordinal, 8) || identity.ordinal > INT32_MAX || !in.integer(identity.units, 8) || !identity.units || identity.units > INT32_MAX ||
                !in.integer(denominator, 4) || !denominator || denominator > INT32_MAX) return false;
            identity.denominator = static_cast<uint32_t>(denominator);
            if (!restored.identities.emplace(native_name, std::move(identity)).second) return false;
        }
    }
    if (!in.bytes.empty()) return false;
    *this = std::move(restored); return true;
}
bool RecipeState::capture(std::string& out, std::string_view prefix) const {
    return capture(out, std::vector<std::string_view>{prefix});
}
bool RecipeState::capture(std::string& out, const std::vector<std::string_view>& prefixes) const {
    RecipeState selected = *this;
    for (auto item = selected.requests_.begin(); item != selected.requests_.end();) {
        if (std::any_of(prefixes.begin(), prefixes.end(), [&](auto prefix) { return item->first.compare(0, prefix.size(), prefix) == 0; })) ++item;
        else item = selected.requests_.erase(item);
    }
    for (const auto& [key, progress] : selected.requests_) {
        (void)key;
        const auto& request = progress.request;
        const auto parent = request.parent_request.empty() ?
            request.parent_actor.substr(0, request.parent_actor.rfind('/')) : request.parent_request;
        if (!parent.empty() && !selected.requests_.count(parent)) return false;
    }
    for (auto item = selected.credits_.begin(); item != selected.credits_.end();) {
        if (selected.requests_.count(item->first.request)) ++item;
        else item = selected.credits_.erase(item);
    }
    selected.watches_.clear();
    return selected.capture(out);
}
bool RecipeState::capture(std::string& out) const {
    out.clear();
    if (plan_.empty() || requests_.size() > max_requests || credits_.size() > max_requests) return false;
    std::string bytes("EAPR\7", 5); text(bytes, plan_); text(bytes, map_); integer(bytes, requests_.size(), 4);
    size_t total_actors = 0, total_members = 0;
    for (const auto& [key, value] : requests_) {
        total_actors += value.actors.size();
        if (value.reserved || total_actors > max_actors) return false;
        text(bytes, key); text(bytes, value.request.owner); text(bytes, value.request.origin); text(bytes, value.request.label);
        integer(bytes, value.request.logical_mask, 8); integer(bytes, value.request.logical_units, 4);
        integer(bytes, value.request.physical_units, 4); integer(bytes, value.request.maximum_emissions, 8);
        integer(bytes, value.request.credit_limit, 4); integer(bytes, value.request.final_credit_units, 4);
        text(bytes, value.request.parent_actor);
        text(bytes, value.request.parent_request);
        integer(bytes, value.emitted, 8); integer(bytes, value.dead, 8); integer(bytes, value.removed, 8);
        integer(bytes, value.closed, 1); integer(bytes, value.empty_completed, 1); integer(bytes, value.actors.size(), 4);
        for (const auto& [actor, state] : value.actors) {
            (void)actor; integer(bytes, state.ordinal, 8); text(bytes, state.native_name); integer(bytes, state.memberships.size(), 4);
            total_members += state.memberships.size();
            if (total_members > max_actors) return false;
            for (const auto& member : state.memberships) { text(bytes, member.owner); text(bytes, member.label); }
        }
        if (bytes.size() > max_bytes) return false;
    }
    integer(bytes, credits_.size(), 4);
    for (const auto& [scope, dead] : credits_) { credit_scope(bytes, scope); integer(bytes, dead, 8); }
    integer(bytes, watches_.size(), 4);
    for (const auto& [key, watch] : watches_) {
        text(bytes, key); integer(bytes, watch.mask, 8); text(bytes, watch.owner); text(bytes, watch.label);
        integer(bytes, watch.baseline.size(), 4);
        for (const auto& [scope, dead] : watch.baseline) { credit_scope(bytes, scope); integer(bytes, dead, 8); }
        if (bytes.size() > max_bytes) return false;
    }
    if (bytes.size() > max_bytes) return false;
    out = std::move(bytes); return true;
}
bool RecipeState::restore(std::string_view bytes, std::string_view expected_plan, std::string_view expected_map) {
    if (bytes.size() > max_bytes || bytes.substr(0, 5) != std::string_view("EAPR\7", 5)) return false;
    Reader in{bytes.substr(5)}; RecipeState restored;
    uint64_t count = 0, total_actors = 0, total_members = 0;
    if (!in.text(restored.plan_, 191) || !in.text(restored.map_, 191) || restored.plan_ != expected_plan ||
        restored.map_ != expected_map || !identifier(restored.plan_) || !identifier(restored.map_) ||
        !in.integer(count, 4) || count > max_requests) return false;
    for (uint64_t i = 0; i < count; ++i) {
        std::string key; RecipeProgress value;
        uint64_t units = 0, physical = 0, credit_limit = 0, final_credit = 0, closed = 0, empty_completed = 0, actors = 0;
        if (!in.text(key, 191) || !identifier(key) || !in.text(value.request.owner, 191) || !in.text(value.request.origin, 191) ||
            !in.text(value.request.label, 1024) || !in.integer(value.request.logical_mask, 8) ||
            !in.integer(units, 4) || !in.integer(physical, 4) || !in.integer(value.request.maximum_emissions, 8) ||
            !in.integer(credit_limit, 4) || !in.integer(final_credit, 4) ||
            !in.text(value.request.parent_actor, 191) ||
            !in.text(value.request.parent_request, 191) ||
            !in.integer(value.emitted, 8) || !in.integer(value.dead, 8) || !in.integer(value.removed, 8) ||
            !in.integer(closed, 1) || closed > 1 || !in.integer(empty_completed, 1) || empty_completed > 1 ||
            (empty_completed && (physical || !closed)) || (!physical && value.emitted) || !in.integer(actors, 4) ||
            value.dead > value.emitted || value.removed > value.emitted - value.dead ||
            actors != value.emitted - value.dead - value.removed || total_actors + actors > max_actors) return false;
        value.request.logical_units = static_cast<uint32_t>(units);
        value.request.physical_units = static_cast<uint32_t>(physical); value.closed = closed != 0;
        value.request.credit_limit = static_cast<uint32_t>(credit_limit);
        value.request.final_credit_units = static_cast<uint32_t>(final_credit);
        value.empty_completed = empty_completed != 0;
        if (!valid(value.request) || value.emitted > UINT64_MAX / value.request.logical_units) return false;
        std::set<uint64_t> ordinals;
        for (uint64_t n = 0; n < actors; ++n) {
            uint64_t ordinal = 0, members = 0;
            RecipeActor actor;
            if (!in.integer(ordinal, 8) || ordinal >= value.emitted || !ordinals.insert(ordinal).second ||
                !in.text(actor.native_name, 511) || actor.native_name.find('\0') != actor.native_name.npos ||
                !in.integer(members, 4) || total_members + members > max_actors) return false;
            actor.ordinal = ordinal;
            std::set<std::pair<std::string, std::string>> memberships;
            for (uint64_t j = 0; j < members; ++j) {
                RecipeMembership member;
                if (!in.text(member.owner, 191) || !in.text(member.label, 1024) || !valid_member(member) ||
                    !memberships.emplace(member.owner, member.label).second) return false;
                actor.memberships.push_back(std::move(member));
            }
            total_members += members;
            value.actors.emplace(key + "/" + std::to_string(ordinal), std::move(actor));
        }
        total_actors += actors;
        if (!restored.requests_.emplace(key, std::move(value)).second) return false;
    }
    for (const auto& [key, progress] : restored.requests_) {
        (void)key;
        if (!progress.request.parent_actor.empty() && !valid_parent(progress.request.parent_actor, restored.requests_)) return false;
        if (!progress.request.parent_request.empty() && (!restored.requests_.count(progress.request.parent_request) ||
            (!progress.request.parent_actor.empty() && progress.request.parent_actor.substr(0, progress.request.parent_actor.rfind('/')) !=
                progress.request.parent_request))) return false;
    }
    std::map<std::string, uint8_t> ancestry;
    for (const auto& [key, progress] : restored.requests_) {
        (void)progress;
        std::string cursor = key;
        std::vector<std::string> path;
        while (!cursor.empty() && !ancestry[cursor]) {
            ancestry[cursor] = 1; path.push_back(cursor);
            const auto& request = restored.requests_.at(cursor).request;
            cursor = !request.parent_request.empty() ? request.parent_request :
                request.parent_actor.empty() ? "" : request.parent_actor.substr(0, request.parent_actor.rfind('/'));
        }
        if (!cursor.empty() && ancestry[cursor] == 1) return false;
        for (const auto& visited : path) ancestry[visited] = 2;
    }
    if (!in.integer(count, 4) || count > max_requests) return false;
    std::map<std::tuple<std::string, std::string, std::string>, uint64_t> credited;
    for (uint64_t i = 0; i < count; ++i) {
        RecipeCreditScope scope; uint64_t dead = 0;
        if (!in.credit_scope(scope) || !in.integer(dead, 8)) return false;
        const auto request = restored.requests_.find(scope.request);
        if (request == restored.requests_.end() ||
            (!dead && (request->second.request.physical_units || request->second.empty_completed || scope.units)) ||
            dead > request->second.dead + request->second.empty_completed ||
            (scope.units != request->second.request.logical_units &&
             (!request->second.request.credit_limit || scope.units != request->second.request.final_credit_units) &&
              !(!request->second.request.physical_units && !scope.units)) ||
            (scope.units != request->second.request.logical_units && scope.units && dead > 1) ||
            (scope.units && scope.units != request->second.request.logical_units &&
             (request->second.emitted < request->second.request.credit_limit ||
              request->second.actors.count(scope.request + "/" + std::to_string(request->second.request.credit_limit - 1)))) ||
            !restored.credits_.emplace(scope, dead).second) return false;
        auto& total = credited[{scope.request, scope.owner, scope.label}]; total += dead;
        if (total > request->second.dead + request->second.empty_completed) return false;
    }
    for (const auto& [key, value] : restored.requests_)
        if (value.empty_completed && !restored.credits_.count({key, value.request.owner, value.request.label})) return false;
    if (!in.integer(count, 4) || count > max_requests) return false;
    uint64_t total_baselines = 0;
    for (uint64_t i = 0; i < count; ++i) {
        std::string key; RecipeWatch watch; uint64_t baselines = 0;
        if (!in.text(key, 191) || !in.integer(watch.mask, 8) || !watch.mask || watch.mask >= (uint64_t{1} << 44) ||
            !in.text(watch.owner, 191) || !in.text(watch.label, 1024) || !in.integer(baselines, 4) ||
            baselines > restored.credits_.size() || total_baselines + baselines > max_actors ||
            restored.watches_.find(key) != restored.watches_.end() || !restored.watch(key, watch.owner, watch.mask, watch.label)) return false;
        watch.baseline.clear();
        for (uint64_t n = 0; n < baselines; ++n) {
            RecipeCreditScope scope; uint64_t dead = 0;
            if (!in.credit_scope(scope) || !in.integer(dead, 8)) return false;
            const auto found = restored.credits_.find(scope);
            if (found == restored.credits_.end() || !dead || dead > found->second ||
                !selects(restored.requests_.at(scope.request).request, watch.mask) ||
                !selects(scope.owner, scope.label, watch.owner, watch.label) ||
                !watch.baseline.emplace(std::move(scope), dead).second) return false;
        }
        restored.watches_.at(key) = std::move(watch); total_baselines += baselines;
    }
    if (!in.bytes.empty()) return false;
    *this = std::move(restored); return true;
}
}
