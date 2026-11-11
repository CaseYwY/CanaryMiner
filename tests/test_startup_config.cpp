// Strict private wire, actual read-only helper and original admission. CPU/owned files only; no mining or devices.
#include "product/startup_config.h"
#include "product/startup_config_service.h"
#include "product_test_util.h"
#include <fstream>
using namespace om;
int main(int argc,char **argv){
    if(argc!=2||!platformInit())return 2;
    const auto root=std::filesystem::path(makeScratchDir("startup-config"));
    const auto path=root/std::filesystem::u8path(u8"配置-fixture.json");
    StartupConfigRequest request;request.ticket=1;request.target=path.u8string();
    std::string wire="unchanged";
    CHECK(encodeStartupConfigRequest(request,&wire));
    StartupConfigRequest decoded;CHECK(decodeStartupConfigRequest(wire,&decoded)&&decoded.ticket==1&&decoded.target==request.target);
    for(const auto &bad:{wire+" ",wire.substr(0,wire.size()-1),std::string("{\"schema\":\"CMCFGQ1\",\"ticket\":0,\"target\":\"/tmp/x\"}\n"),
        std::string("{\"schema\":\"CMCFGQ1\",\"ticket\":1,\"target\":\"/tmp/x\",\"extra\":true}\n")}){
        CHECK(!decodeStartupConfigRequest(bad,&decoded));CHECK(decoded.target==request.target&&decoded.ticket==1);
    }
    for(const auto ticket:{uint64_t(0),UINT64_MAX}){auto bad=request;bad.ticket=ticket;CHECK(!encodeStartupConfigRequest(bad,&wire));}
    for(const auto &target:{std::string(),std::string(4097,'x'),std::string("bad\npath"),std::string("bad\0path",8)}){
        auto bad=request;bad.target=target;CHECK(!encodeStartupConfigRequest(bad,&wire));
    }
    StartupConfigHeader header;header.ticket=1;header.status=StartupConfigReadStatus::Loaded;header.bytes=startupConfigDocumentLimit;
    CHECK(encodeStartupConfigHeader(header,&wire));StartupConfigHeader parsed;
    CHECK(decodeStartupConfigHeader(wire,&parsed)&&parsed.bytes==startupConfigDocumentLimit);
    for(const auto &bad:{wire+"x",wire.substr(0,wire.size()-1),std::string("{\"schema\":\"CMCFGR1\",\"ticket\":1,\"status\":\"unavailable\",\"bytes\":1}\n"),
        std::string("{\"schema\":\"CMCFGR1\",\"ticket\":1,\"status\":\"loaded\",\"bytes\":1048577}\n"),
        std::string("{\"schema\":\"CMCFGR1\",\"ticket\":1,\"status\":\"other\",\"bytes\":0}\n")}){
        CHECK(!decodeStartupConfigHeader(bad,&parsed));CHECK(parsed.bytes==startupConfigDocumentLimit);
    }
    for(const auto status:{StartupConfigReadStatus::Unavailable,StartupConfigReadStatus::Oversized,static_cast<StartupConfigReadStatus>(99)}){
        auto bad=header;bad.status=status;CHECK(!encodeStartupConfigHeader(bad,&wire));
    }
    std::string bytes="unchanged";
    CHECK(readStartupConfigDocument(request,&header,&bytes)&&header.status==StartupConfigReadStatus::Unavailable&&bytes.empty());
    CHECK(!std::filesystem::exists(path));
    const auto write=[&](const std::string &text){std::ofstream file(path,std::ios::binary);file<<text;};
    const auto original=[&]{std::ifstream file(path,std::ios::binary);return std::string(std::istreambuf_iterator<char>(file),{});};
    for(const auto size:{size_t(0),size_t(70000),startupConfigDocumentLimit,startupConfigDocumentLimit+1}){
        const std::string text(size,' ');write(text);
        CHECK(readStartupConfigDocument(request,&header,&bytes));
        CHECK(header.status==(size<=startupConfigDocumentLimit?StartupConfigReadStatus::Loaded:StartupConfigReadStatus::Oversized));
        CHECK(bytes==(size<=startupConfigDocumentLimit?text:std::string())&&original()==text);
    }
    auto relative=request;relative.target="relative.json";bytes="unchanged";header.ticket=73;
    CHECK(!readStartupConfigDocument(relative,&header,&bytes)&&bytes=="unchanged"&&header.ticket==73);
    StartupConfig output;output.nativeEpoch=73;output.config.worker="unchanged";volatile sig_atomic_t stop=1;
    CHECK(inspectStartupConfig(request.target,&stop,false,&output)==StartupConfigStatus::Stopped);
    stop=0;CHECK(inspectStartupConfig(request.target,&stop,false,nullptr)==StartupConfigStatus::Refused);
    const auto epoch=nativeProcessEpoch();NativeProcessAdmission fault;CHECK(fault.begin());fault.markUnresolved();
    CHECK(inspectStartupConfig(request.target,&stop,false,&output)==StartupConfigStatus::Refused);
    fault.settled(); // synthetic no-creation fact, not native reaping evidence
    {NativeProcessScope old(epoch);CHECK(inspectStartupConfig(request.target,&stop,false,&output)==StartupConfigStatus::Refused);}
    CHECK(output.nativeEpoch==73&&output.config.worker=="unchanged");
    CHECK(inspectStartupConfig("",&stop,false,&output)==StartupConfigStatus::Ready&&output.config.worker==ProductConfig{}.worker);
    const std::string good="{\"schema_version\":1,\"worker\":\"fixture-private-worker\",\"donation\":false}";write(good);
    CHECK(inspectStartupConfig(request.target,&stop,false,&output)==StartupConfigStatus::Ready);
    CHECK(output.config.worker=="fixture-private-worker"&&!output.config.donation&&output.nativeEpoch==nativeProcessEpoch()&&original()==good);
    std::filesystem::create_directory(root/"nested");auto dotted=root/"nested"/".."/path.filename();
    CHECK(inspectStartupConfig(dotted.u8string(),&stop,false,&output)==StartupConfigStatus::Ready&&output.config.worker=="fixture-private-worker");
#if !defined(_WIN32)
    std::filesystem::create_directories(root/"real"/"nested");
    std::filesystem::create_directory_symlink(root/"real"/"nested",root/"link");
    std::ofstream(root/"real"/path.filename(),std::ios::binary)<<"{\"schema_version\":1,\"worker\":\"symlink-original\"}";
    CHECK(inspectStartupConfig((root/"link"/".."/path.filename()).u8string(),&stop,false,&output)==StartupConfigStatus::Ready&&output.config.worker=="symlink-original");
    CHECK(original()==good);
    output.config.worker="fixture-private-worker";
#endif
    const std::string malformed="{broken-private-original";write(malformed);std::string error;
    CHECK(inspectStartupConfig(request.target,&stop,false,&output,&error)==StartupConfigStatus::Refused);
    CHECK(original()==malformed&&output.config.worker=="fixture-private-worker"&&error.find("broken-private-original")==error.npos&&error.find(request.target)==error.npos);
    CHECK(error=="config file rejected: invalid or unsupported config-v1 (schema_version 1 required); private details omitted; original preserved.");
    ProductConfig raw;std::string rawProblem;
    CHECK(!parseConfig("{\"schema_version\":1,\"fixture-private-key\":0}",&raw,&rawProblem)&&rawProblem.find("fixture-private-key")!=rawProblem.npos); // internal parser API/strict schema unchanged
    std::atomic<bool> cancel{false};ProductConfig config;config.worker="unchanged";
    for(const auto *mode:{"wrong-ticket","truncated","extra","bad-header","empty","nonzero"}){
        request.target=(root/mode).u8string();error.clear();
        CHECK(!startup_config_detail::read(std::filesystem::u8path(argv[1]),request,cancel,nativeProcessEpoch(),std::chrono::steady_clock::now(),&config,&error));
        CHECK(config.worker=="unchanged"&&error.find("bad-private-header")==error.npos&&!nativeProcessUnresolved());
    }
    request.target=(root/"valid").u8string();
    CHECK(!startup_config_detail::read(std::filesystem::u8path(argv[1]),request,cancel,nativeProcessEpoch(),std::chrono::steady_clock::now()-std::chrono::seconds(11),&config,&error));
    CHECK(config.worker=="unchanged");
    CHECK(startup_config_detail::read(std::filesystem::u8path(argv[1]),request,cancel,nativeProcessEpoch(),std::chrono::steady_clock::now(),&config,&error));
    CHECK(config.worker=="fixture-loaded");
    std::filesystem::remove_all(root);platformShutdown();
    return finishTests("startup config private canonical wire, exact bounds/read-only originals, real sibling, corrupt receipts and original stop/epoch/budget admission");
}
