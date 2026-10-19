#include "enemy_recipe_state.h"
#include <cstdio>
#include <cstdlib>
#include <limits>

using namespace sentinel::expanded;
#define CHECK(value) do { if (!(value)) { std::fprintf(stderr, "FAIL enemy recipe line %d: %s\n", __LINE__, #value); std::exit(1); } } while (0)

RecipeEmission create(RecipeState& state, const char* request) {
    const auto emission = state.reserve(request); CHECK(emission);
    CHECK(state.created(request, *emission)); return *emission;
}
int main() {
    RecipeState linked;
    CHECK(linked.reset("plan", "game/test"));
    CHECK(linked.request("host", {"manager", "origin-host", "", 16, 1, 1, 1}));
    const auto host = linked.reserve("host"); CHECK(host);
    CHECK(linked.created("host", *host, "native_host_42"));
    RecipeRequest spirit{"manager", "origin-spirit", "", uint64_t{1} << 40, 1, 1, 1};
    spirit.parent_actor = host->actor;
    CHECK(linked.request("spirit", spirit));
    std::string linked_snapshot;
    CHECK(linked.capture(linked_snapshot));
    RecipeState linked_restored;
    CHECK(linked_restored.restore(linked_snapshot, "plan", "game/test") &&
        linked_restored.requests().at("spirit").request.parent_actor == host->actor &&
        linked_restored.requests().at("host").actors.at(host->actor).native_name == "native_host_42");
    spirit.parent_actor = "host/1";
    CHECK(!linked.request("missing-parent", spirit));
    CHECK(linked.request("waiting-host", {"manager", "origin-host", "", 16, 1, 1, 1}));
    spirit.parent_actor = "waiting-host/0"; spirit.parent_request = "waiting-host";
    CHECK(linked.request("waiting-spirit", spirit) && linked.parent_pending(spirit.parent_actor));
    CHECK(linked.capture(linked_snapshot) && linked_restored.restore(linked_snapshot, "plan", "game/test") &&
        linked_restored.parent_pending(spirit.parent_actor));
    CHECK(linked.cancel("waiting-host") && !linked.parent_pending(spirit.parent_actor));
    CHECK(!linked.capture(linked_snapshot, "spirit"));
    CHECK(linked.request("independent/source", {"source", "origin", "", 16, 1, 3, 3}));
    const auto independent = linked.reserve("independent/source"); CHECK(independent);
    CHECK(linked.created("independent/source", *independent, "stable_body"));
    CHECK(linked.join_actor(independent->actor, "adopter", "native_label"));
    CHECK(linked.actor_changed(independent->actor, RecipeActorState::dead));
    CHECK(linked.request("native-replay", {"manager", "origin", "", 16, 1, 1, 1}));
    CHECK(linked.reserve("native-replay"));
    CHECK(linked.capture(linked_snapshot, "independent/") &&
        linked_restored.restore(linked_snapshot, "plan", "game/test"));
    CHECK(linked_restored.requests().size() == 1 &&
        credit_at_least(linked_restored.deaths("adopter", 16, "native_label"), 1) == false &&
        !credit_at_most(linked_restored.deaths("adopter", 16, "native_label"), 0));
    CHECK(create(linked_restored, "independent/source").ordinal == 1);
    RecipeNativeCheckpoint checkpoint;
    checkpoint.plan = "plan"; checkpoint.map = "game/test"; checkpoint.state = linked_snapshot;
    checkpoint.shows["stable_body"] = {12, 13, 14, 1, 0, 0, 0, 1, 0, 0, 0, 1};
    checkpoint.actions[{"independent/source", "native_relay"}] = {2, 0, 3};
    std::string native_snapshot;
    CHECK(checkpoint.capture(native_snapshot));
    RecipeNativeCheckpoint native_restored;
    CHECK(native_restored.restore(native_snapshot, "game/test") &&
        native_restored.shows.at("stable_body")[1] == 13 &&
        native_restored.actions.at({"independent/source", "native_relay"}).units == 2);
    CHECK(!native_restored.restore(native_snapshot, "another/map"));
    CHECK(!native_restored.restore(native_snapshot.substr(0, native_snapshot.size() - 1), "game/test"));
    CHECK(!native_restored.restore(native_snapshot + "trailing", "game/test"));
    CHECK(native_restored.capture(linked_snapshot) && linked_snapshot == native_snapshot);
    checkpoint.shows["stable_body"][0] = std::numeric_limits<float>::quiet_NaN();
    CHECK(!checkpoint.capture(linked_snapshot));
    RecipeState state;
    CHECK(state.reset("plan-012345", "game/test"));
    const RecipeRequest first{"manager-a", "origin-a", "arena", 16, 2, 6, 6};
    CHECK(state.request("a/0", first));
    const auto refused = state.reserve("a/0"); CHECK(refused && refused->ordinal == 0);
    CHECK(!state.reserve("a/0"));
    std::string saved;
    CHECK(!state.capture(saved));
    CHECK(state.release("a/0"));
    const auto retry = state.reserve("a/0"); CHECK(retry && retry->actor == refused->actor);
    CHECK(state.created("a/0", *retry));
    CHECK(!state.created("a/0", *retry));
    std::vector<RecipeEmission> bodies{*retry};
    for (int i = 1; i < 6; ++i) bodies.push_back(create(state, "a/0"));
    CHECK(!state.reserve("a/0"));
    CHECK(state.actor_changed(bodies[5].actor, RecipeActorState::dead));
    CHECK(!state.actor_changed(bodies[5].actor, RecipeActorState::dead));
    CHECK(!credit_at_least(state.deaths("manager-a", 16, "arena"), 1));
    CHECK(state.actor_changed(bodies[1].actor, RecipeActorState::dead));
    CHECK(state.actor_changed(bodies[3].actor, RecipeActorState::dead));
    CHECK(credit_at_least(state.deaths("manager-a", 16, "arena"), 1));
    CHECK(!credit_at_least(state.deaths("manager-a", 16, "arena"), 2));
    CHECK(credit_at_most(state.outstanding("manager-a", 16, "arena"), 1));
    CHECK(!credit_at_most(state.outstanding("manager-a", 16, "arena"), 0));
    CHECK(credit_at_most(state.outstanding("manager-a", 32, "arena"), 0));
    CHECK(credit_at_most(state.outstanding("manager-a", 16, "different"), 0));
    CHECK(credit_at_most(state.outstanding("manager-b", 16, "arena"), 0));
    const auto baseline = state.death_baseline("manager-a", 16, "arena");
    CHECK(state.watch("wait/0", "manager-a", 16, "arena"));
    CHECK(!state.watch("wait/0", "manager-a", 32, "arena"));
    CHECK(credit_at_most(state.deaths("manager-a", 16, "arena", baseline), 0));
    CHECK(state.capture(saved));
    RecipeState restored;
    CHECK(restored.restore(saved, "plan-012345", "game/test"));
    std::string same; CHECK(restored.capture(same) && same == saved);
    CHECK(!restored.restore(saved, "another-plan", "game/test"));
    CHECK(!restored.restore(saved.substr(0, saved.size() - 1), "plan-012345", "game/test"));
    CHECK(restored.capture(same) && same == saved);
    CHECK(restored.actor_changed(bodies[0].actor, RecipeActorState::dead));
    CHECK(!credit_at_least(restored.deaths("manager-a", 16, "arena", baseline), 1));
    CHECK(restored.watched_deaths("wait/0") && !credit_at_least(*restored.watched_deaths("wait/0"), 1));
    CHECK(restored.actor_changed(bodies[2].actor, RecipeActorState::dead));
    CHECK(restored.actor_changed(bodies[4].actor, RecipeActorState::dead));
    CHECK(credit_at_least(*restored.watched_deaths("wait/0"), 1));
    CHECK(restored.end_watch("wait/0") && !restored.watched_deaths("wait/0"));

    CHECK(state.request("pending/0", {"manager-a", "origin-b", "next", 32, 6, 2, 2}));
    const auto reduced = create(state, "pending/0");
    CHECK(state.actor_changed(reduced.actor, RecipeActorState::dead));
    CHECK(credit_at_least(state.deaths("manager-a", 32, "next"), 3));
    CHECK(!credit_at_most(state.outstanding("manager-a", 32, "next"), 0));
    CHECK(state.capture(saved));
    CHECK(restored.restore(saved, "plan-012345", "game/test"));
    const auto pending = restored.reserve("pending/0"); CHECK(pending && pending->ordinal == 1);
    CHECK(restored.release("pending/0"));
    CHECK(restored.cancel("pending/0"));
    CHECK(!restored.reserve("pending/0"));
    CHECK(credit_at_most(restored.outstanding("manager-a", 32, "next"), 0));

    CHECK(state.request("empty/0", {"manager-a", "origin-empty", "empty", 16, 2, 0, 0}));
    CHECK(!state.reserve("empty/0"));
    CHECK(!credit_at_least(state.deaths("manager-a", 16, "empty"), 1));
    CHECK(state.complete_empty("empty/0"));
    CHECK(!state.complete_empty("empty/0"));
    CHECK(credit_at_least(state.deaths("manager-a", 16, "empty"), 2));
    CHECK(credit_at_least(state.deaths("manager-a", 16, "empty", {}, true), 2));
    CHECK(credit_at_most(state.alive("manager-a", 16, "empty"), 0));
    CHECK(state.capture(saved) && restored.restore(saved, state.plan(), state.map()));
    CHECK(credit_at_least(restored.deaths("manager-a", 16, "empty"), 2));
    CHECK(state.request("empty/1", {"manager-a", "origin-empty", "cancelled", 16, 2, 0, 0}));
    CHECK(state.cancel("empty/1") && !state.complete_empty("empty/1"));
    CHECK(!credit_at_least(state.deaths("manager-a", 16, "cancelled"), 1));

    RecipeState partial; CHECK(partial.reset("plan-partial", "game/test"));
    CHECK(partial.request("partial/0", {"manager-a", "origin-partial", "arena", 16, 2, 3, 5, 5, 1}));
    std::vector<RecipeEmission> finite;
    for (unsigned i = 0; i < 5; ++i) finite.push_back(create(partial, "partial/0"));
    CHECK(credit_at_most(partial.alive("manager-a", 16, "arena"), 3));
    CHECK(partial.actor_changed(finite[4].actor, RecipeActorState::dead));
    CHECK(!credit_at_least(partial.deaths("manager-a", 16, "arena"), 1));
    CHECK(partial.capture(saved) && restored.restore(saved, partial.plan(), partial.map()));
    for (unsigned i = 0; i < 4; ++i) CHECK(restored.actor_changed(finite[i].actor, RecipeActorState::dead));
    CHECK(credit_at_most(restored.deaths("manager-a", 16, "arena"), 3));
    CHECK(credit_at_least(restored.deaths("manager-a", 16, "arena"), 3));
    CHECK(!credit_at_least(restored.deaths("manager-a", 16, "arena"), 4));

    CHECK(credit_at_least({{1, 3}, {2, 3}}, 1));
    CHECK(credit_at_most({{1, 3}, {2, 3}}, 1));
    CHECK(!credit_at_least({{1, 3}, {1, 3}}, 1));
    CHECK(credit_at_least({{UINT64_MAX - 1, UINT32_MAX}, {1, 3}}, UINT32_MAX));
    CHECK(!credit_at_least({{1, 0}}, 0));
    CHECK(!credit_at_most({{1, 0}}, 0));
    const std::vector<RecipeCredit> many_cohorts(30000, RecipeCredit{1, 3});
    CHECK(credit_at_least(many_cohorts, 10000));
    CHECK(credit_at_most(many_cohorts, 10000));
    CHECK(credit_at_least({{UINT64_MAX, 1}, {UINT64_MAX, 1}}, UINT64_MAX));
    RecipeState renewable;
    CHECK(renewable.reset("plan-012345", "game/test"));
    CHECK(renewable.request("renewable", {"manager-a", "origin-renewable", "", 16, 1, 3, 0}));
    for (unsigned i = 0; i < 10000; ++i) {
        const auto actor = create(renewable, "renewable");
        CHECK(renewable.actor_changed(actor.actor, RecipeActorState::dead));
        CHECK(!renewable.actor_changed(actor.actor, RecipeActorState::dead));
    }
    CHECK(renewable.requests().at("renewable").actors.empty());
    CHECK(renewable.capture(saved) && saved.size() < 256);
    CHECK(restored.restore(saved, "plan-012345", "game/test"));
    CHECK(credit_at_least(restored.deaths("manager-a", 16, ""), 3333));
    CHECK(create(restored, "renewable").ordinal == 10000);
    std::vector<RecipeHealth> health(6, RecipeHealth{{2, 6}, 100, 100});
    for (unsigned index : {5U, 1U, 3U}) health[index].current = 0;
    CHECK(health_at_most(health, 0.5f) == true);
    CHECK(health_at_most(health, 0.49999997f) == false);
    health[0].current = 50;
    CHECK(health_at_most(health, 0.5f) == true);
    CHECK(health_at_most({{{6, 2}, 100, 100}, {{6, 2}, 100, 0}}, 0.5f) == true);
    CHECK(health_at_most({{{2, 6}, 200, 100}, {{6, 2}, 100, 50}}, 0.5f) == true);
    CHECK(health_at_most({{{1, 3}, 100, 200}}, 1) == false);
    CHECK(health_at_most({{{1, 3}, 100, 200}}, 2) == true);
    CHECK(!health_at_most({}, 0.5f));
    CHECK(!health_at_most({{{1, 0}, 100, 0}}, 0.5f));
    CHECK(!health_at_most({{{1, 3}, 100, std::numeric_limits<float>::quiet_NaN()}}, 0.5f));
    RecipeState migration;
    CHECK(migration.reset("plan", "map"));
    CHECK(migration.request("source", {"manager-a", "origin", "before", 16, 1, 3, 3}));
    const auto migrated = create(migration, "source");
    CHECK(migration.detach_actor(migrated.actor, "manager-a", "before"));
    CHECK(migration.join_actor(migrated.actor, "manager-b", "after"));
    CHECK(migration.join_actor(migrated.actor, "manager-b", "after"));
    CHECK(credit_at_most(migration.alive("manager-a", 16, "before"), 0));
    CHECK(!credit_at_most(migration.outstanding("manager-a", 16, "before"), 0));
    CHECK(!credit_at_most(migration.alive("manager-b", 16, "after"), 0));
    CHECK(migration.capture(saved) && restored.restore(saved, "plan", "map"));
    CHECK(restored.actor_changed(migrated.actor, RecipeActorState::dead));
    CHECK(credit_at_most(restored.deaths("manager-a", 16, "before"), 0));
    CHECK(!credit_at_most(restored.deaths("manager-b", uint64_t{1} << 44, "after"), 0));
    const auto future = create(restored, "source");
    CHECK(!credit_at_most(restored.alive("manager-a", 16, "before"), 0));
    CHECK(credit_at_most(restored.alive("manager-b", 16, "after"), 0));
    CHECK(restored.join_actor(future.actor, "manager-b", "shared"));
    CHECK(restored.actor_changed(future.actor, RecipeActorState::dead));
    CHECK(!credit_at_most(restored.deaths("manager-a", 16, "before"), 0));
    CHECK(!credit_at_most(restored.deaths("manager-b", 16, "shared"), 0));
    CHECK(restored.native_quota("source", 1));
    CHECK(!restored.reserve("source"));
    CHECK(restored.capture(saved) && migration.restore(saved, "plan", "map"));
    CHECK(migration.native_quota("source", 4));
    const auto in_flight = migration.reserve("source"); CHECK(in_flight);
    CHECK(migration.cancel("source") && !migration.capture(saved));
    CHECK(migration.created("source", *in_flight));
    CHECK(!migration.reserve("source") && migration.capture(saved));
    std::puts("PASS recipe admission, exact cohort credits, pending emissions and checkpoint state");
}
