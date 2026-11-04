#include "probe_v2.h"
#include "client_payload.h"
#include <cstdio>
#include <io.h>
#include <fcntl.h>

namespace {
using namespace sentinel;
bool input_scope(uint16_t operation, const generated::Bytes& body, generated::scope& scope, uint16_t& type) {
    if (operation == 1) {
        generated::read_envelope read; generated::Bytes payload;
        if (!v2::split_read(body, read, payload)) return false;
        scope = read.scope; type = read.command_type; return true;
    }
    if (operation == 2) {
        generated::submission submit; generated::Bytes payload;
        if (!v2::split_submission(body, submit, payload)) return false;
        scope = submit.reference.scope; type = submit.reference.command_type; return true;
    }
    generated::command_reference reference;
    if (operation == 5) {
        generated::ack ack;
        if (!generated::decode(body, ack)) return false;
        reference = ack.reference;
    } else if ((operation != 3 && operation != 4) || !generated::decode(body, reference)) return false;
    scope = reference.scope; type = reference.command_type; return true;
}
}
sentinel::Inspection binary_probe(uint16_t command_type) {
    using namespace sentinel;
    Inspection out; out.result = ProbeResult::usage;
    _setmode(_fileno(stdin), _O_BINARY);
    generated::Bytes bytes(generated::max_frame + 1);
    bytes.resize(std::fread(bytes.data(), 1, bytes.size(), stdin));
    if (bytes.size() < 32 || bytes.size() > generated::max_frame) return out;
    generated::wire_header_2 header;
    if (!v2::header(generated::Bytes(bytes.begin(), bytes.begin()+32), header, false) || header.payload_length != bytes.size()-32) return out;
    const generated::Bytes body(bytes.begin()+32, bytes.end()); generated::scope scope; uint16_t type = 0;
    if (!input_scope(header.operation, body, scope, type) || (type != command_type && !(command_type == 22 && (type == 23 || type == 24)))) return out;
    out = query(scope.pid, 2000);
    if (out.result != ProbeResult::ok) return out;
    V2Connection connection;
    out.result = connection.connect(scope.pid, 2000, &scope.instance_id);
    if (out.result != ProbeResult::ok) return out;
    const auto response = connection.exchange(header.operation, body, 2000);
    out.result = client_transport(response); out.win32_error = response.win32_error;
    if (out.result != ProbeResult::ok) return out;
    generated::Bytes value;
    if (header.operation == 1) {
        if (response.body.size() < 36) { out.result = ProbeResult::invalid_response; return out; }
        value.assign(response.body.begin()+36,response.body.end());
    } else {
        generated::command_reply reply; generated::sc_diagnostic_result execution; v2::CommandError error;
        if (!v2::command_response(response.body,reply,error,execution,value)) { out.result = ProbeResult::invalid_response; return out; }
        if (error != v2::CommandError::none) { out.result = ProbeResult::capability_unavailable; return out; }
        if (value.empty() && client_publication(type)) { out.result = ProbeResult::capability_unavailable; return out; }
        client_execution(type, generated::from_wire(execution), out);
    }
    if (!value.empty() && !client_result(type, value, out)) out.result = ProbeResult::invalid_response;
    return out;
}
