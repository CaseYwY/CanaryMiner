// Actual inherited local helper + owned synthetic preference files. No production miner, device, game, provider or network.
#include "product/preferences_protocol.h"
#include "product/duplex_task_process.h"
#include "product/tariff_declaration.h"
#include "product_test_util.h"
#include <atomic>
#include <fstream>
#include <sstream>
#include <thread>
namespace {
namespace fs=std::filesystem;
void put(const fs::path &path,const std::string &text){std::ofstream file(path,std::ios::binary|std::ios::trunc);file<<text;CHECK(file.good());}
std::string read(const fs::path &path){std::ifstream file(path,std::ios::binary);return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};}
bool exchange(om::DuplexTaskProcess &process,const om::PreferencesRequest &request,om::PreferencesReply *reply){
    using namespace om;std::string wire;if(!encodePreferencesRequest(request,&wire)||!process.send(wire))return false;
    std::string received;const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(8);bool flushed=false;
    while(std::chrono::steady_clock::now()<until){
        const auto polled=process.poll();flushed=flushed||polled.flushed;
        if(polled.bytes.size()>preferencesFrameLimit-received.size())return false;
        received+=polled.bytes;
        const auto end=received.find('\n');
        if(end!=received.npos)return flushed&&end+1==received.size()&&decodePreferencesReply(received,reply)&&reply->ticket==request.ticket&&reply->kind==request.kind;
        if(polled.channelFault||polled.eof||polled.state!=DuplexTaskState::Running)return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}
}
int main(int argc,char **argv){using namespace om;if(argc!=2)return 2;
    const auto worker=fs::absolute(fs::u8path(argv[1]));
    const auto root=fs::temp_directory_path()/("canary-preferences-native-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code error;const bool owned=fs::create_directory(root,error);CHECK(owned);if(!owned)return finishTests("preferences native scratch refusal");
    struct Cleanup{fs::path root;~Cleanup(){std::error_code ignored;fs::remove_all(root,ignored);}} cleanup{root};
    std::atomic<bool> cancel{false};DuplexTaskProcess process;
    CHECK(process.start(worker,cancel));
    PreferencesRequest request;request.ticket=1;request.operation=PreferencesOperation::Init;
    for(unsigned i=0;i<8;++i)request.targets[i]=(root/(std::string(preferencesKindName(static_cast<PreferencesKind>(i)))+".json")).u8string();
    const auto paths=request.targets;PreferencesReply reply;
    CHECK(exchange(process,request,&reply)&&reply.status==PreferencesReplyStatus::Ready&&fs::is_empty(root));
    std::array<std::string,8> documents{};std::array<uint64_t,8> reviews{};uint64_t ticket=1;
    std::ifstream fixture("config/tariff-declaration.example.json");std::stringstream text;text<<fixture.rdbuf();JsonValue tariff;CHECK(parseTariffDeclaration(text.str(),&tariff,nullptr));
    for(unsigned i=0;i<8;++i){
        request={};request.ticket=++ticket;request.kind=static_cast<PreferencesKind>(i);
        CHECK(exchange(process,request,&reply)&&reply.status==PreferencesReplyStatus::Loaded&&reply.review==ticket&&!fs::exists(fs::u8path(paths[i])));
        reviews[i]=reply.review;documents[i]=i==7?tariffJson(tariff)+"\n":reply.document;
        request={};request.ticket=++ticket;request.kind=static_cast<PreferencesKind>(i);request.operation=PreferencesOperation::Save;request.review=reviews[i];request.document=documents[i];
        CHECK(exchange(process,request,&reply)&&reply.status==PreferencesReplyStatus::Saved&&reply.review==ticket&&reply.document==documents[i]);reviews[i]=reply.review;
        CHECK(read(fs::u8path(paths[i]))==documents[i]);
    }
    // A 131-KiB escaped request traverses real partial native writes; invalid document is refused before mutation.
    request={};request.ticket=++ticket;request.kind=PreferencesKind::Ui;request.operation=PreferencesOperation::Save;request.review=reviews[0];request.document.assign(preferencesDocumentLimit,'"');
    std::string wire;CHECK(encodePreferencesRequest(request,&wire)&&wire.size()>65536);
    CHECK(exchange(process,request,&reply)&&reply.status==PreferencesReplyStatus::Rejected&&read(fs::u8path(paths[0]))==documents[0]);
    put(fs::u8path(paths[0]),documents[0]+"\n");request.document=documents[0];request.ticket=++ticket;
    CHECK(exchange(process,request,&reply)&&reply.status==PreferencesReplyStatus::Unknown&&!reply.review&&reply.document.empty()&&read(fs::u8path(paths[0]))==documents[0]+"\n");
    put(fs::u8path(paths[0]),documents[0]);request.ticket=++ticket;
    CHECK(exchange(process,request,&reply)&&reply.status==PreferencesReplyStatus::Rejected&&read(fs::u8path(paths[0]))==documents[0]);
    request={};request.ticket=++ticket;CHECK(exchange(process,request,&reply)&&reply.status==PreferencesReplyStatus::Loaded);const auto review=reply.review;
    request={};request.ticket=++ticket;request.operation=PreferencesOperation::Save;request.review=review;request.document=documents[0];
    CHECK(exchange(process,request,&reply)&&reply.status==PreferencesReplyStatus::Saved&&read(fs::u8path(paths[0]))==documents[0]);
    process.requestStop();CHECK(process.terminate(1000));
    for(unsigned i=0;i<8;++i)CHECK(read(fs::u8path(paths[i]))==documents[i]);
    return finishTests("actual local preference helper, all eight stores, partial frames and conservative unknown/reopen");
}
