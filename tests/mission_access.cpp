#include "campaign_menu.h"
#include "sentinel_inspection.h"
#include "protocol_v2.h"
#include <cassert>
#include <cstring>

using namespace sentinel;
namespace {
void publish(campaign_menu::Menu& menu, uint64_t revision, bool unlocked = true) {
    sc_campaign_request request{}; std::memset(request.namespace_id, 'a', 64);
    request.revision = revision; request.count = 2;
    request.row.id = 1; request.row.flags = SC_CAMPAIGN_REVEALED | SC_CAMPAIGN_UNLOCKED | SC_CAMPAIGN_HUB;
    strcpy_s(request.row.map, "game/sp/hub/hub");
    assert(!menu.request(campaign_row_operation, request, true).status);
    request.index = 1; request.row = {}; request.row.id = 2;
    request.row.flags = SC_CAMPAIGN_REVEALED | (unlocked ? SC_CAMPAIGN_UNLOCKED : 0);
    strcpy_s(request.row.map, "game/sp/destination/destination");
    assert(!menu.request(campaign_row_operation, request, true).status);
    assert(!menu.request(campaign_commit_operation, request, true).status);
}
sc_evidence_scope scope(uint64_t generation = 5) {
    sc_evidence_scope out{}; out.process.pid = 7; out.process.process_created = 13;
    out.process.instance_id[0] = 1; out.process.lifecycle_generation = generation;
    std::memset(out.namespace_id, 'a', 64); out.save_binding[0] = 1;
    out.readiness = SC_EVIDENCE_READY; strcpy_s(out.map_name, "game/sp/hub/hub"); return out;
}
}
int main() {
    campaign_menu::Menu menu; publish(menu, 1);
    auto before = scope(), destination = scope(6);
    strcpy_s(destination.map_name, "game/sp/destination/destination");
    auto shown = menu.projection();
    assert(!menu.access(2, before).entry_usable);
    menu.entries_usable(shown, before);
    assert(menu.access(2, before).entry_usable && !menu.access(2, before).destination_ready);
    menu.selected(2); menu.loaded(destination.map_name); menu.destination_ready(destination);
    assert(!menu.access(2, destination).destination_ready);
    menu.load_requested(2, shown, before);
    menu.destination_ready(destination);
    assert(!menu.access(2, destination).destination_ready);
    menu.load_requested(2, shown, before); menu.loaded(destination.map_name);
    auto same_generation = destination; same_generation.process.lifecycle_generation = 5;
    menu.destination_ready(same_generation); assert(!menu.access(2, before).destination_ready);
    menu.destination_ready(destination);
    auto result = menu.access(2, destination);
    assert(result.destination_ready && result.revision == 1 && result.destination_revision == 1);
    assert(result.stage_id == 2 && result.destination_stage_id == 2 && result.menu_scope.process.lifecycle_generation == 5);
    menu.load_requested(2, shown, before);
    auto wrong = destination; strcpy_s(wrong.map_name, "game/sp/wrong/wrong");
    menu.destination_ready(wrong); menu.destination_ready(destination);
    assert(!menu.access(2, destination).destination_ready);
    menu.load_requested(2, shown, before);
    wrong = destination; ++wrong.save_binding[0]; menu.destination_ready(wrong); menu.destination_ready(destination);
    assert(!menu.access(2, destination).destination_ready && !menu.access(2, wrong).entry_usable);
    menu.load_requested(2, shown, before);
    menu.entries_usable(shown, destination); menu.loaded(destination.map_name);
    auto reloaded = destination; ++reloaded.process.lifecycle_generation;
    menu.destination_ready(reloaded);
    assert(!menu.access(2, reloaded).destination_ready);
    wrong = before; wrong.readiness = SC_EVIDENCE_UNPROVEN;
    assert(!menu.access(2, wrong).entry_usable);
    menu.load_requested(2, shown, before); publish(menu, 2); menu.destination_ready(destination);
    assert(!menu.access(2, destination).destination_ready);
    publish(menu, 3, false); shown = menu.projection(); menu.entries_usable(shown, before);
    assert(!menu.access(2, before).entry_usable);
    assert(!menu.access(99, before).entry_usable);
    campaign_menu::Menu locked; publish(locked, 1, false); locked.entries_usable(locked.projection(), before);
    assert(!locked.access(2, before).entry_usable);
    generated::Bytes bytes;
    assert(generated::encode(generated::to_wire(result), bytes));
    generated::sc_mission_access_result decoded;
    assert(generated::decode(bytes, decoded) && decoded.destination_ready);
}
