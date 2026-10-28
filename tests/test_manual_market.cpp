#include "product/manual_market.h"
#include "product/manual_market_settings.h"
#include "product/settings.h"
#include "product/product_cli.h"
#include "product_test_util.h"
#include <fstream>
using namespace om;
namespace {
constexpr uint64_t T=1800000000ULL, M=1000;
ManualMarketPreferences declared() {ManualMarketPreferences p; p.enabled=p.fxEnabled=true; p.prlUsdt="2.5"; p.usdtUsd="0.8"; p.prlAsOfUtc=T-10; p.fxAsOfUtc=T-20; p.confirmedUtc=T; p.validitySeconds=60; return p;}
std::string lines(const PublicMarketView &v){std::string text=v.heading+"\n"; for(const auto &s:v.lines)text+=s+"\n"; return text;}
bool has(const PublicMarketView &v,const std::string &s){return lines(v).find(s)!=std::string::npos;}
std::string replace(std::string text,const std::string &old,const std::string &value){const auto n=text.find(old); CHECK(n!=std::string::npos); if(n!=std::string::npos)text.replace(n,old.size(),value); return text;}
void edit(ManualMarketSettingsPanel &p,unsigned field,const std::string &value) {p.field=field; p.key('\r',{},T); for(char c:value)p.key(c,{},T); p.key('\r',{},T);}
}
int main(){
    std::string output,error;
    CHECK(canonicalManualPrice("0002.5000",&output)&&output=="2.5");
    CHECK(canonicalManualPrice("0.000000000000000001",&output));
    CHECK(canonicalManualPrice(std::string(38,'9'),&output));
    for(const auto &s:std::vector<std::string>{"","0","0.0","-0","-1","+1",".5","1.","1e3","NaN","Infinity","$1","1,2","1\n2",std::string(39,'9'),"0.0000000000000000001"})CHECK(!canonicalManualPrice(s,&output));
    ManualMarketPreferences p=declared(),parsed;
    CHECK(manualMarketValid(p)&&parseManualMarket(manualMarketJson(p),&parsed,&error)&&parsed==p);
    const auto json=manualMarketJson(p);
    const auto before=parsed;
    for(const auto &s:std::vector<std::string>{"{}","[]",json+"x",replace(json,"\"schema_version\": 1","\"schema_version\": 2"),replace(json,"pearl-mainnet-native-prl","unqualified-asset"),
        replace(json,"\"token_quote\": \"USDT\"","\"token_quote\": \"USD\""),replace(json,"\"prl_usdt\": \"2.5\"","\"prl_usdt\": 2.5"),replace(json,"\"validity_seconds\": 60","\"validity_seconds\": 0"),replace(json,"\"enabled\": true","\"enabled\": 1"),
        replace(json,"\"max_fx_skew_seconds\": 30","\"max_fx_skew_seconds\": 3601"),replace(json,"\"prl_as_of_utc\": 1799999990","\"prl_as_of_utc\": 1800000000000"),
        replace(json,"\"enabled\": true","\"enabled\": true, \"enabled\": false"),replace(json,"\"schema_version\": 1","\"schema_version\": 1, \"unrecognized\": 0"),
        replace(json,"\"prl_usdt\": \"2.5\"","\"prl_usdt\": \"2.5\\u001b[2J\""),std::string(4097,' ')}) {
        CHECK(!parseManualMarket(s,&parsed,&error)); CHECK(parsed==before);
    }
    CHECK(parseManualMarket(replace(json,"\"2.5\"","\"0002.5000\""),&parsed,&error)&&parsed.prlUsdt=="2.5");
    auto incomplete=p; incomplete.prlAsOfUtc=0; CHECK(!manualMarketValid(incomplete));
    incomplete=p; incomplete.prlUsdt.clear(); CHECK(!manualMarketValid(incomplete));
    incomplete=p; incomplete.fxAsOfUtc=0; CHECK(!manualMarketValid(incomplete));
    incomplete=p; incomplete.prlAsOfUtc=T+1; CHECK(!confirmManualMarket(&incomplete,T,&error));
    incomplete.enabled=false; CHECK(confirmManualMarket(&incomplete,T,&error)); // explicit OFF remains possible for a future reference
    auto projector=makeManualMarketProjector(p,T,M);
    const auto initial=projector(T,M);
    CHECK(initial.enabled&&has(initial,"USER DECLARATION - NOT LIVE")&&has(initial,"2.50000000 USDT/PRL")&&has(initial,"0.80000000 USD/USDT")&&has(initial,"2.00000000 USD/PRL"));
    CHECK(has(initial,"1h equal-time mean: N/A")&&has(initial,"age 10s"));
    CHECK(has(projector(T+40,M+40000),"2.00000000 USD/PRL"));
    CHECK(has(projector(T+40,M+40001),"2.50000000 USDT/PRL")&&has(projector(T+40,M+40001),"FX USD per USDT: N/A (stale"));
    CHECK(has(projector(T+50,M+50000),"2.50000000 USDT/PRL")&&has(projector(T+50,M+50001),"PRL / USDT: N/A (stale"));
    CHECK(has(projector(T-1,M),"UTC/monotonic clock mismatch"));
    CHECK(has(projector(T+51,M+51000),"UTC/monotonic clock mismatch")); // correcting clocks never clears the latch
    auto backwards=makeManualMarketProjector(p,T,M);
    CHECK(has(backwards(T+50,M+50001),"stale manual input"));
    CHECK(has(backwards(T+50,M+50000),"UTC/monotonic clock mismatch")); // one-ms regression could otherwise revive the token reference
    auto mismatch=makeManualMarketProjector(p,T,M);
    CHECK(has(mismatch(T+10,M),"UTC/monotonic clock mismatch"));
    CHECK(has(mismatch(T+10,M+10000),"UTC/monotonic clock mismatch"));
    CHECK(has(makeManualMarketProjector(p,T,M)(T,M),"2.00000000 USD/PRL")); // explicit reconstruction is a separate activation
    CHECK(has(initial,"2.00000000 USD/PRL")); // prior value is not changed by later expiry
    auto disabled=p; disabled.enabled=false; CHECK(!makeManualMarketProjector(disabled,T,M)(T,M).enabled);
    auto noFx=p; noFx.fxEnabled=false; noFx.fxAsOfUtc=T+1000;
    CHECK(has(makeManualMarketProjector(noFx,T,M)(T,M),"2.50000000 USDT/PRL")&&has(makeManualMarketProjector(noFx,T,M)(T,M),"no assumed peg"));
    auto skew=p; skew.prlAsOfUtc=T; skew.fxAsOfUtc=T-40;
    CHECK(has(makeManualMarketProjector(skew,T,M)(T,M),"reference-time skew exceeds"));
    skew.maxFxSkewSeconds=40; CHECK(has(makeManualMarketProjector(skew,T,M)(T,M),"2.00000000 USD/PRL"));
    auto future=p; future.confirmedUtc=T+1;
    CHECK(has(makeManualMarketProjector(future,T,M)(T+10,M+10000),"N/A: invalid")); // invalid at activation cannot become a fresh declaration by waiting
    auto savedAgain=p; CHECK(confirmManualMarket(&savedAgain,T+1000,&error));
    CHECK(has(makeManualMarketProjector(savedAgain,T+1000,9000)(T+1000,9000),"N/A (stale manual input)"));
    auto tiny=p; tiny.prlUsdt="0.000000000000000001"; CHECK(has(makeManualMarketProjector(tiny,T,M)(T,M),"<0.00000001 USDT/PRL"));
    auto rounding=p; rounding.prlUsdt="0.123456785"; rounding.usdtUsd="1";
    CHECK(has(makeManualMarketProjector(rounding,T,M)(T,M),"0.12345678 USD/PRL"));
    rounding.prlUsdt="0.123456795"; CHECK(has(makeManualMarketProjector(rounding,T,M)(T,M),"0.12345680 USD/PRL"));
    DashModel model; model.publicMarket=initial; DashView view; view.columns=120; view.rows=0;
    const auto main=renderDashboard(model,view);
    CHECK(main.find("MANUAL/OFFLINE PRICE REFERENCES")!=std::string::npos&&main.find("PUBLIC MARKET REFERENCES")==std::string::npos);
    CHECK(main.find("PRODUCTION AND REFERENCE VALUATION")==std::string::npos&&main.find("Estimated net")==std::string::npos);
    CHECK(renderPlainSummary(model,static_cast<time_t>(T)).find("2.50000000")==std::string::npos);
    {
        const auto dir=std::filesystem::path(makeScratchDir("manual-market")), file=dir/std::filesystem::u8path("synthetic-\xe6\xb5\x8b\xe8\xaf\x95.json");
        ManualMarketStore store(file); ManualMarketPreferences loaded;
        CHECK(!store.load(nullptr,&error));
        CHECK(!store.save(p,&error)&&store.load(&loaded,&error)&&!loaded.enabled&&!std::filesystem::exists(file));
        CHECK(store.save(p,&error)); ManualMarketStore stale(file); CHECK(stale.load(&loaded,&error)&&loaded==p);
        auto changed=p; changed.prlUsdt="3"; CHECK(store.save(changed,&error));
        CHECK(!stale.save(p,&error)&&error.find("externally")!=std::string::npos);
        CHECK(stale.load(&loaded,&error)&&loaded.prlUsdt=="3");
        {auto lockPath=file; lockPath+=".lock"; RuntimePreferencesLease lease; CHECK(lease.acquire(lockPath)&&!stale.save(loaded,&error));}
        CHECK(stale.save(loaded,&error));
        {std::ofstream out(file); out<<replace(json,"\"schema_version\": 1","\"schema_version\": 2");}
        const auto known=loaded; CHECK(!stale.load(&loaded,&error)&&loaded==known);
        CHECK(std::filesystem::file_size(file)>0);
        CHECK(!stale.save(p,&error)); // invalid replacement has not become an authorized write target
        std::filesystem::remove(file);
        std::filesystem::create_directory(file);
        CHECK(!stale.load(&loaded,&error)&&!stale.save(p,&error)&&std::filesystem::is_directory(file));
        std::filesystem::remove(file);
        {std::ofstream out(file); out<<std::string(4097,' ');}
        CHECK(!stale.load(&loaded,&error)&&loaded==known&&std::filesystem::file_size(file)==4097);
#if !defined(_WIN32)
        std::filesystem::remove(file);
        const auto target=dir/"synthetic-target.json";
        {std::ofstream out(target); out<<json;}
        std::filesystem::create_symlink(target,file);
        CHECK(!stale.load(&loaded,&error)&&!stale.save(p,&error)&&std::filesystem::is_symlink(file));
        CHECK(std::filesystem::file_size(target)==json.size());
#endif
        std::filesystem::remove_all(dir);
    }
    ManualMarketSettingsPanel panel; panel.open({},{}); int saves=0;
    auto save=[&](ManualMarketPreferences &,std::string *){++saves; return true;};
    panel.key(' ',save,T); panel.key('v',save,T); panel.key('\r',save,T);
    CHECK(saves==0&&panel.field==1&&panel.draft.enabled&&!panel.active.enabled&&!panel.message.empty());
    panel.key('\r',save,T); CHECK(panel.key('q',save,T)==ManualMarketSettingsResult::None); panel.key('\r',save,T);
    CHECK(panel.page==ManualMarketSettingsPage::Edit&&!panel.message.empty());
    CHECK(panel.key(3,save,T)==ManualMarketSettingsResult::Quit); panel.key(27,save,T);
    edit(panel,1,"2.5000"); CHECK(panel.draft.prlUsdt=="2.5");
    panel.field=2; panel.key('n',save,T); CHECK(panel.draft.prlAsOfUtc==T);
    panel.field=3; panel.key(' ',save,T); edit(panel,4,"0.8"); panel.field=5; panel.key('n',save,T);
    edit(panel,6,"60"); edit(panel,7,"0"); CHECK(panel.draft.maxFxSkewSeconds==0);
    panel.key('v',save,T); panel.key('\r',[](ManualMarketPreferences &,std::string *e){*e="synthetic save failure"; return false;},T);
    CHECK(panel.dirty()&&!panel.active.enabled&&panel.message=="synthetic save failure");
    panel.key('\r',save,T); CHECK(saves==1&&!panel.dirty()&&panel.active.enabled&&panel.active.confirmedUtc==T);
    for(unsigned field=0;field<8;++field)for(int width:{60,80,120})for(int rows:{12,24}) {
        panel.field=field; const auto text=panel.render(width,rows,ColorLevel::None);
        CHECK(text.find("> ")!=std::string::npos&&std::count(text.begin(),text.end(),'\n')<=rows);
        size_t start=0; for(size_t end=text.find('\n');end!=std::string::npos;end=text.find('\n',start)){CHECK(end-start<static_cast<size_t>(width)); start=end+1;}
    }
    panel.field=2; edit(panel,2,"1800000000000"); CHECK(panel.page==ManualMarketSettingsPage::Edit&&panel.message.find("milliseconds")!=std::string::npos); panel.key(27,save,T);
    panel.field=1; panel.key('\r',save,T); panel.key('q',save,T); panel.key('\r',save,T);
    const auto invalid=panel.render(60,12,ColorLevel::None);
    auto readable=invalid; std::replace(readable.begin(),readable.end(),'\n',' ');
    CHECK(readable.find("no exponent or symbol.")!=std::string::npos&&std::count(invalid.begin(),invalid.end(),'\n')<=12);
    panel.key(27,save,T);
    panel.field=0; panel.key(' ',save,T); panel.key(27,save,T); CHECK(panel.page==ManualMarketSettingsPage::Leave);
    CHECK(panel.key('d',save,T)==ManualMarketSettingsResult::Close&&panel.draft==panel.active);
    SettingsPanel parent; parent.open({}); parent.draft.color=false; parent.runtime.draft.workWaitMs=99; parent.group=3; parent.groupsFocused=false;
    CHECK(parent.key(' ',{})==SettingsResult::OpenManualMarket); parent.manual.open(p,{}); parent.manual.field=6;
    CHECK(parent.key('\r',{})==SettingsResult::None&&parent.manual.page==ManualMarketSettingsPage::Edit);
    CHECK(parent.key('q',{})==SettingsResult::None&&parent.runtime.draft.workWaitMs==99&&!parent.draft.color);
    parent.key(27,{}); parent.key(27,{}); CHECK(!parent.manual.visible&&parent.visible&&parent.dirty());
    ManualMarketContext context; context.running=true; context.effective=p;
    auto external=p; external.prlUsdt="3"; panel.open(external,context);
    CHECK(panel.render(120,30,ColorLevel::None).find("Current PRL input: 2.5")!=std::string::npos);
    const char *args[]={"canary","--no-market"}; CliParse cli;
    CHECK(parseProductCli(2,args,&cli,&error)&&cli.ov.noMarket&&cli.rest.empty());
    return finishTests("manual market");
}
