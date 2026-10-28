#include "product/energy_owner.h"
#include "product/electricity_settings.h"
#include "product/settings.h"
#include "product/supervisor.h"
#include "product_test_util.h"
#include <chrono>
#include <thread>
#include <fstream>
using namespace om;
namespace {
constexpr uint64_t T=1800000000000ULL,M=1000;
ElectricityPreferences declared(){ElectricityPreferences p;p.collectEnergy=p.tariffEnabled=p.exclusionsAcknowledged=true;p.country="synthetic-country";p.region="synthetic-region";p.supplier="synthetic-supplier";p.product="synthetic-plan";p.version="synthetic-v1";p.currency="USD";p.ratePerKwh="0.5";p.effectiveFromUtc=T/1000-100;p.effectiveThroughUtc=T/1000+100000;p.confirmedUtc=T/1000;return p;}
template<class P>bool until(P predicate){const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);while(std::chrono::steady_clock::now()<end){if(predicate())return true;std::this_thread::sleep_for(std::chrono::milliseconds(1));}return predicate();}
bool at(SessionEnergySink &owner,uint64_t mono){return until([&]{return owner.snapshot()->throughMonoMs==mono;});}
bool has(const SessionEnergySnapshot &s,const std::string &needle){for(const auto &line:s.view.lines)if(line.find(needle)!=std::string::npos)return true;return false;}
std::string replaced(std::string text,const std::string &old,const std::string &value){const auto at=text.find(old);CHECK(at!=std::string::npos);if(at!=std::string::npos)text.replace(at,old.size(),value);return text;}
void input(ElectricitySettingsPanel &p,unsigned field,const std::string &value){p.field=field;p.key('\r',{},T/1000);for(char c:value)p.key(c,{},T/1000);p.key('\r',{},T/1000);}
}
int main(){
    ElectricityPreferences p=declared(),parsed;std::string error,text,normalized;
    CHECK(electricityPreferencesValid(p)&&parseElectricityPreferences(electricityPreferencesJson(p),&parsed,&error)&&parsed==p);
    CHECK(canonicalElectricityRate("-000.000",&normalized)&&normalized=="0");
    CHECK(canonicalElectricityRate("-0002.5000",&normalized)&&normalized=="-2.5");
    for(const auto &rate:{"+1","1e2","NaN","1.",".1"," 1","1\n2"})CHECK(!canonicalElectricityRate(rate,&normalized));
    for(const auto &bad:std::vector<std::string>{"{}","[]","{\"schema_version\":2}",electricityPreferencesJson(p)+"x",std::string(8193,' ')}){const auto before=parsed;CHECK(!parseElectricityPreferences(bad,&parsed,&error)&&parsed==before);}
    const auto json=electricityPreferencesJson(p);
    for(const auto &bad:std::vector<std::string>{replaced(json,"\"schema_version\":1","\"schema_version\":2"),replaced(json,"\"rule_model\":\"flat_energy_only\"","\"rule_model\":\"shared_progressive\""),
        replaced(json,"\"measurement_basis\":\"gpu_board_proxy\"","\"measurement_basis\":\"whole_meter\""),replaced(json,"\"collect_energy\":true","\"collect_energy\":1"),
        replaced(json,"\"rate_per_kwh\":\"0.5\"","\"rate_per_kwh\":0.5"),replaced(json,"\"clock\":\"utc\"","\"clock\":\"named_iana\""),
        replaced(json,"\"schema_version\":1","\"schema_version\":1,\"tax_rule\":0"),replaced(json,"\"schema_version\":1","\"schema_version\":1,\"schema_version\":1")})CHECK(!parseElectricityPreferences(bad,&parsed,&error));
    auto invalid=p;invalid.exclusionsAcknowledged=false;CHECK(!electricityPreferencesValid(invalid));
    invalid=p;invalid.ratePerKwh.clear();CHECK(!electricityPreferencesValid(invalid));
    invalid=p;invalid.ratePerKwh="0";CHECK(electricityPreferencesValid(invalid));
    invalid=p;invalid.currency="USDT";CHECK(!electricityPreferencesValid(invalid));
    invalid=p;invalid.country="bad\x1b[2J";CHECK(!electricityPreferencesValid(invalid));
    invalid=p;invalid.fixedOffsetSeconds=3600;CHECK(!electricityPreferencesValid(invalid));invalid.fixedStandardTime=true;CHECK(electricityPreferencesValid(invalid));
    invalid=p;invalid.maximumGapMs=500;CHECK(!electricityPreferencesValid(invalid));
    invalid=p;invalid.effectiveThroughUtc=invalid.effectiveFromUtc;CHECK(!electricityPreferencesValid(invalid));
    {
        const auto root=std::filesystem::path(makeScratchDir("electricity-prefs")),file=root/"synthetic.json";
        ElectricityPreferencesStore store(file),stale(file);ElectricityPreferences loaded;
        CHECK(!store.save(p,&error)&&store.load(&loaded,&error)&&!loaded.collectEnergy&&!std::filesystem::exists(file));
        CHECK(store.save(p,&error)&&stale.load(&loaded,&error)&&loaded==p);
        auto changed=p;changed.ratePerKwh="-1";CHECK(!store.save(changed,&error)&&error.find("NEW effective version")!=std::string::npos);
        changed.version="synthetic-v2";CHECK(store.save(changed,&error)&&!stale.save(p,&error));
        CHECK(stale.load(&loaded,&error)&&loaded.ratePerKwh=="-1");
        auto lock=file;lock+=".lock";{RuntimePreferencesLease held;CHECK(held.acquire(lock)&&!stale.save(loaded,&error));}
        {std::ofstream out(file);out<<"{\"schema_version\":2}";}
        const auto known=loaded;CHECK(!stale.load(&loaded,&error)&&loaded==known&&!stale.save(p,&error));
        CHECK(std::filesystem::file_size(file)>0);std::filesystem::remove_all(root);
    }
    auto owner=makeSessionEnergyOwner();
    uint64_t generation=0;
    auto start=[&](uint64_t g,uint64_t t,uint64_t m,unsigned c,const ElectricityPreferences &prefs){generation=g;return owner->start(g,t,m,c,prefs,PowerOrigin::SyntheticFixture);};
    auto feed=[&](SessionPowerInput observation){return owner->submit(generation,observation);};
    auto future=p;future.confirmedUtc=T/1000+1;CHECK(!start(1,T,M,1,future));
    CHECK(!start(0,T,M,1,p)&&!start(1,T,M,0,p)&&!start(1,T,M,65,p));
    CHECK(start(1,T,M,2,p)&&!owner->start(2,T,M,2,p,PowerOrigin::SyntheticFixture));
    const auto empty=owner->snapshot();CHECK(has(*empty,"N/A (no covered power interval)")&&empty->coveredDeviceMs==0);
    CHECK(feed({2,T,M,1000})==EnergyAdmission::Invalid);
    for(unsigned i=0;i<2;++i)CHECK(feed({i,T,M,i?200000u:100000u})==EnergyAdmission::Queued);
    for(unsigned i=0;i<2;++i)CHECK(feed({i,T+1000,M+1000,i?200000u:100000u})==EnergyAdmission::Queued);
    CHECK(until([&]{return owner->snapshot()->coveredDeviceMs==2000;}));
    const auto first=owner->snapshot();CHECK(first->knownEnergyNumerator=="300000"&&first->knownCostNumerator=="150000"&&first->pricedDeviceMs==2000);
    CHECK(first->coveredDeviceMs==first->requestedDeviceMs&&has(*first,"not whole-PC")&&has(*first,"Not a utility bill"));
    CHECK(empty->knownEnergyNumerator=="0");
    CHECK(feed({0,T+2000,M+2000,{}})==EnergyAdmission::Queued);
    CHECK(at(*owner,M+2000));CHECK(owner->snapshot()->knownEnergyNumerator=="400000");
    CHECK(feed({0,T+3000,M+3000,0})==EnergyAdmission::Queued);CHECK(at(*owner,M+3000));CHECK(owner->snapshot()->knownEnergyNumerator=="400000");
    CHECK(feed({0,T+4000,M+4000,100000})==EnergyAdmission::Queued);CHECK(at(*owner,M+4000));CHECK(owner->snapshot()->knownEnergyNumerator=="400000"); // observed zero is covered, not missing
    CHECK(feed({0,T+20000,M+20000,100000})==EnergyAdmission::Queued);CHECK(at(*owner,M+20000));CHECK(owner->snapshot()->knownEnergyNumerator=="400000"); // overlong gap not bridged
    CHECK(feed({0,T+23001,M+20001,100000})==EnergyAdmission::Queued);
    CHECK(until([&]{return owner->snapshot()->state==SessionEnergyState::ClockFault;}));
    CHECK(feed({0,T+24000,M+24000,100000})==EnergyAdmission::Stopped);
    owner->stop();CHECK(!start(1,T,M,1,p));
    auto negative=p;negative.ratePerKwh="-2";CHECK(start(2,T,M,1,negative));
    CHECK(owner->submit(1,{0,T,M,1000})==EnergyAdmission::StaleGeneration);
    CHECK(feed({0,T,M,1000})==EnergyAdmission::Queued&&feed({0,T+1000,M+1000,1000})==EnergyAdmission::Queued&&at(*owner,M+1000));
    CHECK(owner->snapshot()->knownEnergyNumerator=="1000"&&owner->snapshot()->knownCostNumerator=="-2000");
    const auto beforeStop=owner->snapshot();owner->requestStop();const auto stopped=owner->snapshot();std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK(stopped->state==SessionEnergyState::Stopped&&owner->snapshot()->revision==stopped->revision&&beforeStop->state==SessionEnergyState::Running);owner->stop();
    auto expired=p;expired.effectiveThroughUtc=T/1000;CHECK(start(3,T,M,1,expired));
    feed({0,T,M,1000});feed({0,T+1000,M+1000,1000});CHECK(at(*owner,M+1000));
    CHECK(owner->snapshot()->coveredDeviceMs==1000&&owner->snapshot()->pricedDeviceMs==0&&has(*owner->snapshot(),"cost: N/A"));owner->stop();
    auto hidden=p;hidden.showEnergy=false;CHECK(start(4,T,M,1,hidden));CHECK(!owner->snapshot()->view.enabled);owner->stop();
    auto missing=p;missing.tariffEnabled=false;CHECK(start(5,T,M,1,missing));feed({0,T,M,{}});feed({0,T+1000,M+1000,{}});CHECK(at(*owner,M+1000));
    CHECK(owner->snapshot()->coveredDeviceMs==0&&has(*owner->snapshot(),"no electricity cost estimate"));owner->stop();
    // Independent admission oracle: any dropped input breaks the next accepted hold, even when the time gap is small.
    auto rapid=p;rapid.tariffEnabled=false;rapid.maximumGapMs=60000;CHECK(start(6,T,M,1,rapid));
    uint64_t previous=0,expected=0,lastAccepted=M,drops=0;bool seen=false,discontinuity=false;
    for(uint64_t i=0;i<10000;++i){const auto mono=M+i*500;const auto status=feed({0,T+i*500,mono,1000});
        if(status==EnergyAdmission::Full){++drops;discontinuity=true;}
        else if(status==EnergyAdmission::Queued){if(seen&&!discontinuity&&mono-previous<=rapid.maximumGapMs)expected+=mono-previous;seen=true;previous=lastAccepted=mono;discontinuity=false;}
        else CHECK(false);
    }
    CHECK(at(*owner,lastAccepted));const auto barrier=M+10000*500;
    CHECK(feed({0,T+10000*500,barrier,1000})==EnergyAdmission::Queued);
    if(seen&&!discontinuity&&barrier-previous<=rapid.maximumGapMs)expected+=barrier-previous;
    CHECK(at(*owner,barrier));CHECK(drops>0&&owner->snapshot()->dropped==drops&&owner->snapshot()->coveredDeviceMs==expected&&owner->snapshot()->knownEnergyNumerator==std::to_string(expected));owner->stop();
    auto edge=p;edge.effectiveFromUtc=T/1000;edge.effectiveThroughUtc=T/1000+5;CHECK(start(7,T,M,1,edge));
    for(uint64_t delta:{0ULL,2000ULL,3000ULL,4000ULL})CHECK(feed({0,T+delta,M+delta,1000})==EnergyAdmission::Queued);
    CHECK(at(*owner,M+4000));CHECK(owner->snapshot()->coveredDeviceMs==4000&&owner->snapshot()->pricedDeviceMs==1000&&owner->snapshot()->knownCostNumerator=="500");owner->stop();
    CHECK(start(8,T,M,1,p));feed({0,T+2000,M,1000});feed({0,T+3000,M+1000,1000});CHECK(at(*owner,M+1000));
    CHECK(owner->snapshot()->state==SessionEnergyState::Running&&owner->snapshot()->knownEnergyNumerator=="1000");
    feed({0,T+3002,M+1001,1000});CHECK(until([&]{return owner->snapshot()->state==SessionEnergyState::ClockFault;}));CHECK(owner->snapshot()->knownEnergyNumerator=="1000");owner->stop();
    CHECK(!owner->start(9,T,M,1,p,PowerOrigin::Unknown));
    auto zero=p;zero.ratePerKwh="0";CHECK(start(9,T,M,1,zero));
    CHECK(feed({0,T,M,1})==EnergyAdmission::Queued&&feed({0,T+500,M+500,UINT32_MAX})==EnergyAdmission::Queued&&at(*owner,M+500));
    CHECK(owner->snapshot()->knownEnergyNumerator=="0.5"&&has(*owner->snapshot(),"<0.000001 kWh"));
    feed({0,T+1000,M+1000,0});CHECK(at(*owner,M+1000));
    CHECK(owner->snapshot()->knownEnergyNumerator=="2147483648"&&owner->snapshot()->knownCostNumerator=="0"&&has(*owner->snapshot(),"cost: 0.000000 USD"));
    CHECK(owner->snapshot()->evidence.basis==PowerBasis::GpuBoard&&owner->snapshot()->evidence.origin==PowerOrigin::SyntheticFixture&&has(*owner->snapshot(),"SYNTHETIC FIXTURE"));owner->stop();
    DashModel model;model.energy=first->view;DashView view;view.rows=0;view.columns=120;
    const auto display=renderDashboard(model,view);CHECK(display.find("SESSION GPU-BOARD ENERGY")!=std::string::npos&&display.find("PRODUCTION AND REFERENCE VALUATION")==std::string::npos&&display.find("Estimated net")==std::string::npos);
    CHECK(renderPlainSummary(model,static_cast<time_t>(T/1000)).find("synthetic-supplier")==std::string::npos);
    ElectricitySettingsPanel panel;panel.open({},{});unsigned saves=0;auto save=[&](ElectricityPreferences &,std::string *){++saves;return true;};
    panel.field=4;panel.key(' ',save,T/1000);panel.key('v',save,T/1000);panel.key('\r',save,T/1000);CHECK(saves==0&&panel.field==5&&!panel.message.empty());
    panel.key(' ',save,T/1000);input(panel,6,"synthetic-country");input(panel,7,"synthetic-region");input(panel,8,"synthetic-supplier");input(panel,9,"synthetic-product");input(panel,10,"synthetic-v1");input(panel,11,"USD");
    input(panel,12,"0");panel.field=15;panel.key('n',save,T/1000);input(panel,16,std::to_string(T/1000+10000));
    panel.key('v',save,T/1000);panel.key('\r',save,T/1000);CHECK(saves==1&&!panel.dirty()&&panel.active.tariffEnabled&&panel.active.ratePerKwh=="0");
    panel.field=12;panel.key('\r',save,T/1000);CHECK(panel.key('q',save,T/1000)==ElectricitySettingsResult::None);panel.key('\r',save,T/1000);CHECK(panel.page==ElectricitySettingsPage::Edit&&!panel.message.empty());
    CHECK(panel.key(3,save,T/1000)==ElectricitySettingsResult::Quit);panel.key(27,save,T/1000);
    panel.field=6;panel.key('\r',save,T/1000);for(unsigned i=0;i<65;++i)panel.key('x',save,T/1000);panel.key('\r',save,T/1000);
    CHECK(panel.page==ElectricitySettingsPage::Edit&&panel.draft.country==panel.active.country&&panel.inputRejected);panel.key(27,save,T/1000);
    panel.key('\r',save,T/1000);panel.key(0xe4,save,T/1000);panel.key('\r',save,T/1000);CHECK(panel.page==ElectricitySettingsPage::Edit&&panel.inputRejected);panel.key(27,save,T/1000);
    for(unsigned field=0;field<17;++field)for(int columns:{60,80,120})for(int rows:{12,24}){panel.field=field;const auto screen=panel.render(columns,rows,ColorLevel::None);CHECK(screen.find("> ")!=std::string::npos&&std::count(screen.begin(),screen.end(),'\n')<=rows);}
    panel.field=0;panel.key(' ',save,T/1000);panel.key(27,save,T/1000);CHECK(panel.page==ElectricitySettingsPage::Leave&&panel.key('d',save,T/1000)==ElectricitySettingsResult::Close&&!panel.dirty());
    SettingsPanel parent;parent.open({});parent.draft.color=false;parent.group=2;parent.groupsFocused=false;CHECK(parent.key('\r',{})==SettingsResult::OpenElectricity);parent.electricity.open({},{});parent.electricity.field=6;
    CHECK(parent.key('\r',{})==SettingsResult::None&&parent.key('q',{})==SettingsResult::None&&parent.dirty());
    ElectricitySettingsContext live;live.running=true;live.effective=p;panel.open(p,live);panel.draft.ratePerKwh="9";panel.key('v',save,T/1000);
    CHECK(panel.render(120,40,ColorLevel::None).find("Current tariff: 0.5 USD/kWh | synthetic-v1")!=std::string::npos);
    GpuTelemetry telemetry;telemetry.valid=true;telemetry.powerMilliwatts=123456;CHECK(!telemetry.powerValid);
    WorkerView gpu;gpu.tel=telemetry;gpu.telValid=true;SupervisorOptions options;CHECK(aggregateSnapshot(options,{gpu},1,"running").devices[0].powerWatts<0);
    gpu.tel.powerValid=true;gpu.tel.powerMilliwatts=0;CHECK(aggregateSnapshot(options,{gpu},1,"running").devices[0].powerWatts==0);
    const char *args[]={"canary","--no-energy"};CliParse cli;CHECK(parseProductCli(2,args,&cli,&error)&&cli.ov.noEnergy&&cli.rest.empty());
    return finishTests("session energy/settings");
}
