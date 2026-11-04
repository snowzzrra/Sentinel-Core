#include "protocol_v2.h"
#include "wire_fixtures.h"
#include <cassert>
#include <cstring>

using namespace sentinel;
using generated::Bytes;
template<class T> T roundtrip(const Bytes& bytes) {
    T value{}; assert(generated::decode(bytes, value));
    Bytes encoded; assert(generated::encode(value, encoded) && encoded == bytes); return value;
}
void result_cases(const std::map<std::string, wire_fixtures::Fixture>& fixtures) {
    const auto& currency = fixtures.at("currency_result").bytes;
    const auto battery = roundtrip<generated::sc_currency_result>(currency);
    assert(battery.flags == 7 && battery.balance_before == 0 && battery.gained_after == 9);
    const auto progression = roundtrip<generated::sc_progression_result>(fixtures.at("progression_result").bytes);
    assert(progression.suit_known == 63 && progression.suit_owned == 45 && progression.slayer_key_quantity == 2);
    for (const auto proof : progression.mastery_proof) assert(proof == SC_EFFECT_UNPROVEN);
    const auto mission = roundtrip<generated::sc_mission_access_result>(fixtures.at("mission_access_result").bytes);
    assert(mission.entry_usable && mission.destination_ready && mission.revision == 17 && mission.destination_revision == 17);
    roundtrip<generated::progression_payload>(fixtures.at("progression_read").bytes);
    roundtrip<generated::mission_access_payload>(fixtures.at("mission_access_read").bytes);
    const auto hello = roundtrip<generated::hello_result>(fixtures.at("hello_evidence").bytes);
    assert(hello.supported.size() == 3);
    for (const auto& supported : hello.supported) assert(supported.command_type >= 53 && supported.payload_revision == 1);
}
void frame_cases(const std::map<std::string, wire_fixtures::Fixture>& fixtures) {
    const auto& currency = fixtures.at("currency_result").bytes;
    for (const auto name : {"currency_observation", "progression_observation", "mission_access_observation", "battery_command_result"}) {
        const auto& bytes = fixtures.at(name).bytes;
        const Bytes body(bytes.begin() + 32, bytes.end());
        generated::wire_header_2 header;
        assert(v2::header(Bytes(bytes.begin(), bytes.begin() + 32), header, true));
        Bytes encoded; assert(v2::frame(header, body, encoded) && encoded == bytes);
        if (header.operation == 1) {
            roundtrip<generated::scope>(Bytes(body.begin(), body.begin() + 36));
            const Bytes result(body.begin() + 36, body.end());
            if (std::strcmp(name, "currency_observation") == 0) assert(roundtrip<generated::sc_currency_result>(result).kind == SC_CURRENCY_OBSERVE);
            else if (std::strcmp(name, "progression_observation") == 0) roundtrip<generated::sc_progression_result>(result);
            else roundtrip<generated::sc_mission_access_result>(result);
        } else {
            generated::command_reply reply; generated::sc_diagnostic_result execution;
            v2::CommandError error; Bytes result;
            assert(v2::command_response(body, reply, error, execution, result));
            assert(error == v2::CommandError::none && reply.reference.command_id == "battery" && result == currency);
            generated::submission input{{reply.reference.scope, reply.reference.namespace_id, "battery", 53, 1}, {}, 1000};
            input.save_binding[0] = 9;
            sc_currency_request request{}; std::memset(request.namespace_id, 'a', 64);
            request.kind = SC_CURRENCY_GRANT; request.currency = 4; request.amount = 2; request.expected_gained = 7;
            std::array<uint8_t, 32> fingerprint;
            assert(v2::fingerprint(input, request, fingerprint) && fingerprint == reply.reference.fingerprint);
        }
    }
}
int main(int argc, char** argv) {
    assert(argc == 2);
    const auto fixtures = wire_fixtures::read(argv[1]);
    result_cases(fixtures); frame_cases(fixtures);
}
