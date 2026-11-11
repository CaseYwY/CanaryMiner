// Actual owned CPU helper/files and native state machines. No GPU, game, provider or installed miner access.
#include "product/preferences_settings.h"
#include "product_test_util.h"
#include <fstream>
#include <new>
#include <sstream>
#include <thread>
namespace {
using namespace om;namespace fs=std::filesystem;
thread_local bool rejectAllocations=false;thread_local unsigned allocationAttempts=0;
std::string read(const fs::path &path){std::ifstream file(path,std::ios::binary);return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};}
void put(const fs::path &path,const std::string &text){std::ofstream file(path,std::ios::binary|std::ios::trunc);file<<text;CHECK(file.good());}
template<class Predicate>bool await(Predicate predicate){const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(10);do{if(predicate())return true;std::this_thread::sleep_for(std::chrono::milliseconds(2));}while(std::chrono::steady_clock::now()<end);return false;}
std::shared_ptr<const PreferencesSettingsCompletion> completed(PreferencesSettings &bridge){
    std::shared_ptr<const PreferencesSettingsCompletion> result;CHECK(await([&]{auto next=bridge.poll();if(next)result=next;return !bridge.pending();}));return result;
}
void hide(SettingsPanel &p){p.runtime.visible=p.manual.visible=p.electricity.visible=p.tariff.visible=p.game.visible=p.poolSecurity.visible=p.language.visible=false;}
void load(PreferencesSettings &bridge,SettingsPanel &panel,PreferencesKind kind){hide(panel);CHECK(bridge.load(kind));auto receipt=completed(bridge);CHECK(receipt&&receipt->reply->status==PreferencesReplyStatus::Loaded&&receipt->reply->kind==kind);}
void previewEnd(PreferencesSettings &bridge){for(unsigned n=0;n<1000;++n){bridge.render(80,24,ColorLevel::None);bridge.key(kKeyPageDown,999999999,999999999);}bridge.render(80,24,ColorLevel::None);}
}
bool canaryNativeAllocationFault(){if(!rejectAllocations)return false;++allocationAttempts;return true;}
int main(int argc,char **argv){using namespace om;if(argc!=3)return 4;const std::string scenario=argv[2];
    if(scenario!="basic"&&scenario!="conflict"&&scenario!="lost"&&scenario!="cancel"&&scenario!="allocation"&&scenario!="large")return 4;
    const auto root=fs::temp_directory_path()/("canary-preferences-settings-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code error;const bool owned=fs::create_directory(root,error);CHECK(owned);if(!owned)return finishTests("preference settings scratch refusal");
    struct Cleanup{fs::path root;~Cleanup(){std::error_code ignored;fs::remove_all(root,ignored);}} cleanup{root};
    std::array<fs::path,8> targets{};for(unsigned i=0;i<8;++i)targets[i]=root/(std::string(preferencesKindName(static_cast<PreferencesKind>(i)))+".json");
    SessionIntent intent;const auto token=intent.explicitStart();PreferencesOwnerPolicy policy;policy.operationTimeoutMs=scenario=="lost"?1000:5000;policy.reapMs=1000;
    PreferencesOwner owner(fs::absolute(fs::u8path(argv[1])),targets,intent,token,policy);SettingsPanel panel;panel.open({});PreferencesSettings bridge(owner,panel);
    bridge.context.runtime.running=bridge.context.game.running=bridge.context.pool.running=bridge.context.language.running=bridge.context.manual.running=bridge.context.electricity.running=true;
    bridge.context.runtime.effective.workWaitMs=17;bridge.context.game.effective=GamePreferences{};bridge.context.manualProjectionAvailable=true;
    CHECK(owner.start());CHECK(await([&]{return owner.snapshot().state==PreferencesOwnerState::Ready;}));CHECK(fs::is_empty(root));
    if(scenario=="large"){
        load(bridge,panel,PreferencesKind::Pool);const auto current=poolSecurityPreferencesJson(panel.poolSecurity.active);
        auto &draft=panel.poolSecurity.draft;draft.users.resize(8);
        const auto host=std::string(60,'a')+"."+std::string(60,'b')+"."+std::string(60,'c')+"."+std::string(40,'d')+".example";
        for(auto &rule:draft.users){rule.mode=PoolSecurityMode::PreferTls;rule.tlsUrl="stratum+ssl://"+host+":443";rule.plaintextUrl="stratum+tcp://"+host+":444";rule.plaintextConsent=true;}
        draft.donation=draft.users.back();draft.donation.plaintextUrl="stratum+tcp://tail-marker.example:444";
        const auto original=poolSecurityPreferencesJson(draft);CHECK(original.size()>4096&&validPoolSecurityPreferences(draft));put(targets[5],current+"\n");
        panel.poolSecurity.page=PoolSecuritySettingsPage::Review;bridge.key('\r',1700000000,12345);CHECK(bridge.pending()&&!completed(bridge)&&bridge.overlay());
        CHECK(owner.snapshot().uncertain&&owner.snapshot().uncertain->document==original);
        CHECK(bridge.render(80,24,ColorLevel::None).find("tail-marker")==std::string::npos);bridge.key('r',0,0);
        const auto narrow=bridge.render(20,12,ColorLevel::None);CHECK(narrow.find("Resize for review")!=std::string::npos&&narrow.find("Q/^C Stop")!=std::string::npos&&narrow.find("tail-marker")==std::string::npos);
        bool tail=false;for(unsigned n=0;n<250;++n){auto frame=bridge.render(80,24,ColorLevel::None);frame.erase(std::remove(frame.begin(),frame.end(),'\n'),frame.end());tail=tail||frame.find("tail-marker.example")!=std::string::npos;bridge.key(kKeyPageDown,0,0);}
        CHECK(tail&&owner.snapshot().uncertain->document==original&&read(targets[5])==current+"\n");owner.stop();CHECK(owner.snapshot().settled);
        return finishTests("large private canonical original scrolls beyond 4096 bytes without truncation, unsafe controls or implicit replay");
    }
    load(bridge,panel,PreferencesKind::Ui);const auto original=panel.active;panel.draft.hideWorker=!panel.draft.hideWorker;panel.page=SettingsPage::Review;
    const auto expected=uiPreferencesJson(panel.draft);const auto saved=panel.draft;
    if(scenario=="allocation"){
        bool threw=false;rejectAllocations=true;allocationAttempts=0;try{bridge.key('\r',1700000000,12345);}catch(...){threw=true;}rejectAllocations=false;
        CHECK(!threw&&allocationAttempts>0&&!bridge.pending()&&panel.active==original&&panel.draft==saved&&owner.snapshot().reviews[0]!=0&&!fs::exists(targets[0]));
    }
    if(scenario=="conflict")put(targets[0],uiPreferencesJson(original)+"\n");
    bridge.key('\r',1700000000,12345);CHECK(bridge.pending()&&panel.active==original&&panel.page==SettingsPage::Review);
    bridge.key(' ',1700000999,99999);CHECK(panel.draft==saved);CHECK(bridge.render(80,24,ColorLevel::None).find("NOT Saved")!=std::string::npos);
    if(scenario=="cancel"){
        CHECK(await([&]{return owner.snapshot().inFlight;}));bridge.key(27,1700000999,99999);bridge.poll();
        CHECK(!bridge.pending()&&panel.active==original&&owner.snapshot().uncertain&&owner.snapshot().uncertain->document==expected);
        owner.stop();CHECK(owner.snapshot().settled);return finishTests("native preference cancellation never publishes queued Saved and retains issued original");
    }
    auto result=completed(bridge);
    if(scenario=="lost"){
        CHECK(!result&&panel.active==original&&bridge.overlay()&&owner.snapshot().uncertain&&read(targets[0])==expected);
        const auto hidden=bridge.render(80,24,ColorLevel::None);CHECK(hidden.find("hide_worker")==std::string::npos);bridge.key('r',0,0);CHECK(bridge.render(80,24,ColorLevel::None).find("schema_version")!=std::string::npos);
        CHECK(bridge.key(3,0,0)==SettingsResult::Quit);owner.stop();CHECK(owner.snapshot().settled&&panel.active==original);return finishTests("actual committed private preference with lost acknowledgement stays UNKNOWN, locally revealable, never applied");
    }
    if(scenario=="conflict"){
        CHECK(!result&&panel.active==original&&bridge.overlay()&&owner.snapshot().state==PreferencesOwnerState::NeedsInspection);
        bridge.key(' ',0,0);bridge.key('\r',0,0);CHECK(static_cast<bool>(owner.snapshot().uncertain));
        CHECK(bridge.render(80,24,ColorLevel::None).find("hide_worker")==std::string::npos);bridge.key('i',0,0);CHECK(bridge.pending());CHECK(!completed(bridge));
        bridge.key('r',0,0);bridge.key(' ',0,0);bridge.key('\r',0,0);CHECK(static_cast<bool>(owner.snapshot().uncertain)); // revealing is not complete review/ack
        previewEnd(bridge);bridge.key('\t',0,0);previewEnd(bridge);bridge.key(' ',0,0);bridge.key('\r',0,0);
        result=bridge.poll();CHECK(result&&result->reply->status==PreferencesReplyStatus::Loaded&&!bridge.overlay()&&!owner.snapshot().uncertain&&owner.snapshot().acknowledgedUnknown);
        CHECK(panel.active==original&&panel.draft==original&&read(targets[0])==uiPreferencesJson(original)+"\n");
        CHECK(owner.snapshot().submitted==4); // Load, Save, explicit inspection Load only; acknowledgement writes nothing
        owner.stop();CHECK(owner.snapshot().settled);return finishTests("native private full-original/current inspection, explicit acknowledgement, no automatic retry or speculative success");
    }
    CHECK(result&&result->reply->status==PreferencesReplyStatus::Saved&&result->submittedUtc==1700000000&&result->submittedMono==12345&&panel.active==saved&&read(targets[0])==expected&&!bridge.overlay());
    panel.draft.color=!panel.draft.color;const auto unrelated=panel.draft;
    load(bridge,panel,PreferencesKind::Runtime);CHECK(panel.draft==unrelated&&panel.runtime.context.effective.workWaitMs==17);panel.runtime.draft.workWaitMs=23;panel.runtime.page=RuntimeSettingsPage::Review;
    bridge.key('\r',1700000001,12346);CHECK(bridge.pending()&&panel.runtime.active.workWaitMs==0);result=completed(bridge);CHECK(result&&panel.runtime.active.workWaitMs==23&&panel.runtime.context.effective.workWaitMs==17&&panel.draft==unrelated);
    load(bridge,panel,PreferencesKind::Manual);panel.manual.draft.enabled=true;panel.manual.draft.prlUsdt="2";panel.manual.draft.prlAsOfUtc=1699999999;panel.manual.page=ManualMarketSettingsPage::Review;
    bridge.context.publicReferencesRequested=true;bridge.key('\r',1700000002,12347);CHECK(!bridge.pending()&&!panel.manual.active.enabled);
    bridge.context.publicReferencesRequested=false;bridge.context.manualProjectionAvailable=false;bridge.key('\r',1700000002,12347);CHECK(!bridge.pending()&&!panel.manual.active.enabled);
    unsigned prepared=0;bridge.context.manualFactory=[&](const ManualMarketPreferences &p,uint64_t utc,uint64_t mono){CHECK(p.confirmedUtc==1700000002&&utc==1700000002&&mono==12347);++prepared;return ManualMarketProjector{};};
    bridge.context.manualProjectionAvailable=true;bridge.key('\r',1700000002,12347);CHECK(bridge.pending()&&!panel.manual.active.enabled);result=completed(bridge);
    CHECK(result&&result->submittedUtc==1700000002&&result->submittedMono==12347&&panel.manual.active.confirmedUtc==1700000002&&panel.manual.active.prlAsOfUtc==1699999999&&panel.manual.context.effective==panel.manual.active&&prepared==1&&result->manualValue==panel.manual.active);
    load(bridge,panel,PreferencesKind::Electricity);panel.electricity.draft.collectEnergy=true;panel.electricity.page=ElectricitySettingsPage::Review;bridge.key('\r',1700000003,12348);result=completed(bridge);
    CHECK(result&&panel.electricity.active.collectEnergy&&!panel.electricity.context.effective.collectEnergy&&panel.electricity.active.confirmedUtc==1700000003);
    load(bridge,panel,PreferencesKind::Game);panel.game.draft.pollMs=600;panel.game.page=GameSettingsPage::Review;bridge.key('\r',1700000004,12349);result=completed(bridge);CHECK(result&&panel.game.active.pollMs==600&&panel.game.context.effective&&panel.game.context.effective->pollMs==500);
    load(bridge,panel,PreferencesKind::Pool);panel.poolSecurity.draft.users[0].tlsUrl="stratum+ssl://private-preview.example:443";panel.poolSecurity.page=PoolSecuritySettingsPage::Review;bridge.key('\r',1700000005,12350);result=completed(bridge);CHECK(result&&panel.poolSecurity.active.users[0].tlsUrl=="stratum+ssl://private-preview.example:443");
    load(bridge,panel,PreferencesKind::Language);panel.language.draft.language="zh-Hans";panel.language.page=LanguageSettingsPage::Review;bridge.key('\r',1700000006,12351);result=completed(bridge);CHECK(result&&panel.language.active.language=="zh-Hans"&&std::string(effectiveNativeMessageLanguage())=="en");
    load(bridge,panel,PreferencesKind::Tariff);std::ifstream fixture("config/tariff-declaration.example.json");std::stringstream text;text<<fixture.rdbuf();CHECK(parseTariffDeclaration(text.str(),&panel.tariff.draft,nullptr));panel.tariff.page=TariffSettingsPage::Review;
    bridge.key('\r',1800000007,12352);result=completed(bridge);CHECK(result&&panel.tariff.page==TariffSettingsPage::Browse&&read(targets[7])==tariffJson(panel.tariff.active)+"\n");
    CHECK(panel.draft==unrelated);intent.stop();CHECK(!bridge.poll());CHECK(bridge.key(3,0,0)==SettingsResult::Quit);owner.stop();CHECK(owner.snapshot().settled);
    return finishTests("native asynchronous eight-domain settings, exact receipts/original clocks, independent drafts and frozen runtime contexts");
}
