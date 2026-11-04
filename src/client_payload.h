#pragma once
#include "protocol_v2.h"

namespace sentinel {
using CampaignRequest = generated::NativeCampaign;
bool client_payload(uint16_t, uint16_t, const generated::NativeRequest&, generated::Bytes&);
bool client_result(uint16_t, const generated::Bytes&, Inspection&);
void client_execution(uint16_t, const sc_diagnostic_result&, Inspection&);
bool client_publication(uint16_t);
ProbeResult client_transport(const V2Response&);
}
