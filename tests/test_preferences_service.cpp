// Disk service is exercised only on owned synthetic files; no helper/device/network/mining process is started here.
#include "product/preferences_service.h"
#include "product/ui_preferences.h"
#include "product/tariff_declaration.h"
#include "product_test_util.h"
#include <array>
#include <fstream>
#include <sstream>
namespace {
namespace fs=std::filesystem;
void put(const fs::path &path,const std::string &text){std::ofstream file(path,std::ios::binary|std::ios::trunc);file<<text;CHECK(file.good());}
std::string read(const fs::path &path){std::ifstream file(path,std::ios::binary);return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};}
}
int main(){using namespace om;
    const auto root=fs::temp_directory_path()/("canary-preferences-service-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code error;const bool owned=fs::create_directory(root,error);CHECK(owned);if(!owned)return finishTests("preferences service scratch refusal");
    struct Cleanup{fs::path root;~Cleanup(){std::error_code ignored;fs::remove_all(root,ignored);}} cleanup{root};
    PreferencesService service;PreferencesRequest request;request.ticket=2;PreferencesReply reply;reply.document="previous-original";
    CHECK(!service.transact(request,&reply)&&reply.document=="previous-original");
    request.ticket=1;request.operation=PreferencesOperation::Init;
    for(unsigned i=0;i<8;++i)request.targets[i]=(root/(std::string(preferencesKindName(static_cast<PreferencesKind>(i)))+".json")).u8string();
    const auto targets=request.targets;
    {PreferencesService invalid;auto bad=request;bad.targets[0]="relative-fixture.json";CHECK(!invalid.transact(bad,&reply));
        bad=request;bad.targets[1]=bad.targets[0];CHECK(!invalid.transact(bad,&reply));
        bad=request;bad.targets[0]=(root/".."/"outside-fixture.json").u8string();CHECK(!invalid.transact(bad,&reply));}
    CHECK(service.transact(request,&reply)&&reply.status==PreferencesReplyStatus::Ready&&reply.document.empty());
    CHECK(fs::is_empty(root));CHECK(!service.transact(request,&reply));
    std::array<std::string,8> documents{};std::array<uint64_t,8> reviews{};uint64_t ticket=1;
    const auto operation=[&](PreferencesOperation op,unsigned index,uint64_t review=0,const std::string &document=""){
        PreferencesRequest next;next.ticket=++ticket;next.kind=static_cast<PreferencesKind>(index);next.operation=op;next.review=review;next.document=document;
        PreferencesReply result;CHECK(service.transact(next,&result)&&result.ticket==ticket&&result.kind==next.kind);return result;
    };
    for(unsigned i=0;i<8;++i){
        const auto result=operation(PreferencesOperation::Load,i);CHECK(result.status==PreferencesReplyStatus::Loaded&&result.review==ticket&&!result.document.empty());
        documents[i]=result.document;reviews[i]=result.review;CHECK(!fs::exists(fs::u8path(targets[i])));
    }
    // Empty tariff load is an editable template, not a valid tariff or save permission.
    {auto result=operation(PreferencesOperation::Save,7,reviews[7],documents[7]);CHECK(result.status==PreferencesReplyStatus::Rejected&&!fs::exists(fs::u8path(targets[7])));}
    std::ifstream fixture("config/tariff-declaration.example.json");std::stringstream text;text<<fixture.rdbuf();JsonValue tariff;CHECK(parseTariffDeclaration(text.str(),&tariff,nullptr));documents[7]=tariffJson(tariff)+"\n";
    for(unsigned i=0;i<8;++i){
        const auto result=operation(PreferencesOperation::Save,i,reviews[i],documents[i]);
        CHECK(result.status==PreferencesReplyStatus::Saved&&result.review==ticket&&result.document==documents[i]&&read(fs::u8path(targets[i]))==documents[i]);reviews[i]=result.review;
    }
    for(unsigned i=0;i<8;++i){
        const auto path=fs::u8path(targets[i]);
        auto result=operation(PreferencesOperation::Save,i,reviews[(i+1)%8],documents[i]);
        CHECK(result.status==PreferencesReplyStatus::Rejected&&read(path)==documents[i]);
        // Noncanonical/invalid input cannot consume a healthy loaded review or write a file.
        result=operation(PreferencesOperation::Save,i,reviews[i],documents[i]+"\n");CHECK(result.status==PreferencesReplyStatus::Rejected&&read(path)==documents[i]);
        result=operation(PreferencesOperation::Save,i,reviews[i],documents[i]);CHECK(result.status==PreferencesReplyStatus::Saved);reviews[i]=result.review;
        put(path,documents[i]+"\n");result=operation(PreferencesOperation::Save,i,reviews[i],documents[i]);
        CHECK(result.status==PreferencesReplyStatus::Unknown&&result.review==0&&result.document.empty()&&read(path)==documents[i]+"\n");
        put(path,documents[i]);result=operation(PreferencesOperation::Save,i,reviews[i],documents[i]);CHECK(result.status==PreferencesReplyStatus::Rejected&&read(path)==documents[i]);
        result=operation(PreferencesOperation::Load,i);CHECK(result.status==PreferencesReplyStatus::Loaded&&result.document==documents[i]);reviews[i]=result.review;
        result=operation(PreferencesOperation::Save,i,reviews[i],documents[i]);CHECK(result.status==PreferencesReplyStatus::Saved);reviews[i]=result.review;
        put(path,"{malformed-synthetic-preference");result=operation(PreferencesOperation::Load,i);CHECK(result.status==PreferencesReplyStatus::Rejected&&result.document.empty()&&!result.review);
        put(path,documents[i]);result=operation(PreferencesOperation::Save,i,reviews[i],documents[i]);CHECK(result.status==PreferencesReplyStatus::Rejected&&read(path)==documents[i]);
        result=operation(PreferencesOperation::Load,i);CHECK(result.status==PreferencesReplyStatus::Loaded&&result.document==documents[i]);reviews[i]=result.review;
    }
    // Invalid envelope/ticket cannot advance the service's original next-ticket expectation.
    request={};request.ticket=ticket+2;reply.document="previous-original";CHECK(!service.transact(request,&reply)&&reply.document=="previous-original");
    auto result=operation(PreferencesOperation::Load,0);CHECK(result.status==PreferencesReplyStatus::Loaded);
    {PreferencesService disabled;request={};request.ticket=1;request.operation=PreferencesOperation::Init;request.targets[0]=targets[0];CHECK(disabled.transact(request,&reply));
        request={};request.ticket=2;request.kind=PreferencesKind::Runtime;CHECK(disabled.transact(request,&reply)&&reply.status==PreferencesReplyStatus::Rejected&&!reply.review&&reply.document.empty());}
    CHECK(!fs::exists(root/"outside-fixture.json"));
    return finishTests("private helper-side preference load/save review binding, uncertainty and original preservation");
}
