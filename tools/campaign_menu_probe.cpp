#include "sentinel_inspection.h"
#include "protocol.h"
#include <cstdio>
#include <cwchar>
#include <io.h>
#include <fcntl.h>

int campaign_menu_command(int argc,wchar_t** argv) {
    if (argc<2 || std::wcscmp(argv[1],L"--campaign-menu")) return -1;
    if (argc!=2) return 2;
    _setmode(_fileno(stdin),_O_BINARY);
    sentinel::Message message{};
    const auto count=std::fread(message.data(),1,message.size(),stdin);
    uint16_t op=0; sc_campaign_request request{};
    if (sentinel::decode_request(message,count,&op,nullptr,nullptr,nullptr,nullptr,nullptr,&request)!=sentinel::WireResult::ok ||
        op<sentinel::campaign_row_operation || op>sentinel::campaign_inspect_operation) {
        std::puts("{\"result\":\"malformed_request\"}"); return 2;
    }
    const auto r=sentinel::query_campaign(request.execution.expected.pid,2000,op,request);
    if (r.result!=sentinel::ProbeResult::ok) {
        std::printf("{\"result\":\"%s\",\"win32_error\":%u}\n",sentinel::result_name(r.result),r.win32_error);
        return static_cast<int>(r.result);
    }
    const auto& p=r.campaign;
    std::printf("{\"result\":\"ok\",\"core_version\":\"%s\",\"build_id\":\"%s\","
        "\"namespace\":\"%s\",\"status\":%u,\"reason\":%u,\"request_id\":\"%llu\","
        "\"committed_revision\":\"%llu\",\"rendered_revision\":\"%llu\",\"selected_id\":%u,\"loaded_id\":%u}\n",
        r.snapshot.core.version,r.snapshot.core.build_id,p.namespace_id,p.status,p.reason,p.request_id,
        p.committed_revision,p.rendered_revision,p.selected_id,p.loaded_id);
    return 0;
}
