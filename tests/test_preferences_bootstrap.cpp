// Actual supervisor + sibling preference service, only synthetic in-memory fleet/energy/source callbacks. No mining/device setup.
#include "cpu_telemetry_fixture.h"
#include "product/supervisor.h"
#include "product_test_util.h"
#include <fstream>
using namespace om;
namespace {
struct Probe {unsigned fleet=0,fleetStarts=0,launches=0,fleetStops=0,manual=0,market=0,energy=0,energyStarts=0,capture=0,forecast=0;};
class Fleet final:public WorkerFleet {
    Probe &p_;
  public:
    explicit Fleet(Probe &p):p_(p){}
    bool start(unsigned slots,GameGuardTime)override{++p_.fleetStarts;return slots==1;}
    bool launch(unsigned,std::filesystem::path,std::vector<std::string>,unsigned,GameGuardTime)override{++p_.launches;return true;}
    void tick(GameGuardTime,const WorkerFleetEvidence &)override{}
    WorkerFleetState state()const override{return WorkerFleetState::Active;}
    WorkerFleetSnapshot snapshot(unsigned)const override{WorkerFleetSnapshot s;s.requested=s.done=s.reaped=p_.launches!=0;s.exitCode=0;return s;}
    std::vector<std::string> takeLines(unsigned)override{return {};}
    void requestStop()override{}
    void stop()override{++p_.fleetStops;}
};
class Energy final:public SessionEnergySink {
    Probe &p_;ElectricityPreferences expected_;std::shared_ptr<SessionEnergySnapshot> state_=std::make_shared<SessionEnergySnapshot>();
  public:
    Energy(Probe &p,ElectricityPreferences expected):p_(p),expected_(std::move(expected)){}
    bool start(uint64_t generation,uint64_t utc,uint64_t mono,unsigned channels,const ElectricityPreferences &preferences,PowerOrigin origin)override{
        ++p_.energyStarts;CHECK(generation&&utc&&mono&&channels==1&&origin==PowerOrigin::ObservedTelemetry);CHECK(preferences==expected_);state_->state=SessionEnergyState::Running;return true;}
    EnergyAdmission submit(uint64_t,SessionPowerInput)override{return EnergyAdmission::Stopped;}
    void requestStop()override{state_->state=SessionEnergyState::Stopped;}
    void stop()override{requestStop();}
    std::shared_ptr<const SessionEnergySnapshot> snapshot()const override{return state_;}
};
std::string bytes(const std::filesystem::path &p){std::ifstream in(p,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
}
int main(int argc,char **argv){
    CHECK(argc==2);if(argc!=2)return finishTests("preferences bootstrap");
    const std::string mode=argv[1];
    const bool plain=mode=="plain-energy"||mode=="no-energy",badManual=mode=="malformed-manual",badEnergy=mode=="malformed-energy";
    const bool conflict=mode=="conflict",suppressed=mode=="no-market",stopSource=mode=="stop-source",nativeSource=mode=="native-source";
    CHECK(plain||badManual||badEnergy||conflict||suppressed||stopSource||nativeSource||mode=="manual");
    if(!platformInit())return 2;
    const auto root=std::filesystem::path(makeScratchDir("preferences-bootstrap"));
    const auto ui=root/"ui.json",manual=root/"manual.json",electricity=root/"electricity.json";
    const auto beforeUtc=static_cast<uint64_t>(time(nullptr)),beforeMono=supervisorNowMs();
    ManualMarketPreferences declaration;declaration.enabled=true;declaration.prlUsdt="2.5";declaration.prlAsOfUtc=beforeUtc-7;declaration.confirmedUtc=beforeUtc-3;
    ElectricityPreferences energy;energy.collectEnergy=true;energy.confirmedUtc=beforeUtc-5;
    const auto uiBytes=uiPreferencesJson(UiPreferences{}),manualBytes=badManual||suppressed?std::string("malformed private fixture"):manualMarketJson(declaration),energyBytes=badEnergy||mode=="no-energy"?std::string("malformed private fixture"):electricityPreferencesJson(energy);
    std::ofstream(ui,std::ios::binary)<<uiBytes;std::ofstream(manual,std::ios::binary)<<manualBytes;std::ofstream(electricity,std::ios::binary)<<energyBytes;
    Probe probe;volatile sig_atomic_t stop=0;NativeProcessAdmission synthetic;
    SupervisorOptions options;options.version="synthetic";options.exe="must-not-be-executed";options.apiEnabled=false;options.dashboard=!plain;options.stop=&stop;
    options.displaySampleFactory=cpu_fixture::statsOnly();
    WorkerSeed seed;seed.index=0;seed.uuid="fixture.bootstrap";options.seeds={seed};
    options.uiPreferencesPath=ui.u8string();options.manualMarketPreferencesPath=manual.u8string();options.electricityPreferencesPath=electricity.u8string();
    options.noEnergy=mode=="no-energy";options.noMarket=suppressed;
    options.workerFleetFactory=[&](const SessionIntent &,SessionToken)->std::unique_ptr<WorkerFleet>{++probe.fleet;return std::make_unique<Fleet>(probe);};
    options.manualMarketFactory=[&](const ManualMarketPreferences &p,uint64_t utc,uint64_t mono){++probe.manual;
        CHECK(p==declaration);CHECK(utc>=beforeUtc&&utc<=static_cast<uint64_t>(time(nullptr)));CHECK(mono>=beforeMono&&mono<=supervisorNowMs());
        if(stopSource)stop=1;
        if(nativeSource){CHECK(synthetic.begin());synthetic.markUnresolved();synthetic.settled();} // no native creation: synthetic epoch change only
        return ManualMarketProjector{};};
    if(conflict||badManual||suppressed)options.marketReferenceFactory=[&]()->std::unique_ptr<MarketReferenceSink>{++probe.market;return {};};
    options.energyFactory=[&]()->std::unique_ptr<SessionEnergySink>{++probe.energy;return std::make_unique<Energy>(probe,energy);};
    if(badEnergy)options.energyCaptureFactory=[&]()->std::unique_ptr<EnergyCaptureSink>{++probe.capture;return {};};
    options.forecastFactory=[&](bool noMarket,bool noEnergy)->std::unique_ptr<ForecastSink>{++probe.forecast;CHECK(noMarket==suppressed&&noEnergy==(mode=="no-energy"));return {};};
    Supervisor supervisor(std::move(options));const auto result=supervisor.run();
    CHECK(result==(nativeSource?kExitFaultLatched:kExitOk));CHECK(probe.fleet==1&&probe.fleetStarts==1&&probe.fleetStops==1);
    CHECK(probe.launches==(stopSource||nativeSource?0u:1u));
    CHECK(probe.manual==(!plain&&!conflict&&!badManual&&!suppressed?1u:0u));CHECK(probe.market==0);
    const bool collect=!badEnergy&&mode!="no-energy"&&!stopSource&&!nativeSource;
    CHECK(probe.energy==(collect?1u:0u)&&probe.energyStarts==probe.energy);CHECK(probe.capture==0);
    CHECK(probe.forecast==(!badManual&&!badEnergy&&!stopSource&&!nativeSource?1u:0u));
    CHECK(bytes(ui)==uiBytes&&bytes(manual)==manualBytes&&bytes(electricity)==energyBytes);
    std::filesystem::remove_all(root);platformShutdown();return finishTests("preferences bootstrap");
}
