// Owned loopback HTTP, blocking CPU readers and a synthetic fleet only. No GPU/installed worker is touched.
#include "cpu_telemetry_fixture.h"
#include "product/supervisor.h"
#include "product_test_util.h"
using namespace om;
namespace {
template<class Predicate>bool wait(Predicate p){const auto until=supervisorNowMs()+6000;
    while(supervisorNowMs()<until){if(p())return true;std::this_thread::sleep_for(std::chrono::milliseconds(2));}return p();}
struct Probe {
    std::atomic<unsigned> created{0},reads{0},destroyed{0};
    std::atomic<bool> entered{false},release{false},timedOut{false};
    bool block=false,throws=false;unsigned delayMs=0,powerMode=0;
    std::thread::id creator,reader,destroyer;
    std::vector<DisplaySampleTarget> targets;
};
DisplaySampleReading synthetic(const std::string &uuid){
    DisplaySampleReading out;out.statsObserved=out.powerBegan=out.powerFinished=displaySampleTime();
    StatsSnapshot stats;stats.poolState="mining";stats.devices.push_back({});stats.devices[0].rateTmacs=2;stats.devices[0].outcomes.accepted=7;
    out.stats=stats;out.accepted=7;out.telemetry.valid=out.telemetry.powerValid=true;out.telemetry.powerMilliwatts=100000;out.telemetry.deviceUuid=uuid;return out;
}
class Reader final:public DisplaySampleReader {
    Probe &p;
   public:
    Reader(Probe &probe,const std::vector<DisplaySampleTarget> &targets):p(probe){p.targets=targets;p.creator=std::this_thread::get_id();++p.created;}
    ~Reader()override{p.destroyer=std::this_thread::get_id();++p.destroyed;}
    DisplaySampleReading read(size_t slot,const std::atomic<bool> &)override{
        p.reader=std::this_thread::get_id();++p.reads;p.entered=true;
        if(p.throws)throw 1;
        if(p.block&&!wait([&]{return p.release.load();}))p.timedOut=true;
        auto out=synthetic(p.targets[slot].uuid);
        if(p.delayMs)std::this_thread::sleep_for(std::chrono::milliseconds(p.delayMs));
        out.powerFinished=displaySampleTime();
        if(p.powerMode==1)out.telemetry.powerValid=false;
        if(p.powerMode==2)out.telemetry.deviceUuid="GPU-00112233-4455-6677-8899-aabbccddee99";
        return out;
    }
};
DisplaySampleFactory factory(Probe &p){return [&p](const std::vector<DisplaySampleTarget> &targets){return std::make_unique<Reader>(p,targets);};}
const std::vector<DisplaySampleTarget> targets={{"GPU-00112233-4455-6677-8899-aabbccddee01",12345},{"GPU-00112233-4455-6677-8899-aabbccddee02",12346}};
void lifetime(){
    {Probe p;SupervisorSampling owner;owner.requestStop();CHECK(!owner.start(targets,factory(p))&&!owner.request({1,1})&&!owner.take());CHECK(p.created==0);}
    for(auto invalid:std::vector<std::vector<DisplaySampleTarget>>{{},{{"",1}},{{"fixture",0}},{{"fixture",65536}},std::vector<DisplaySampleTarget>(257,targets[0])}){
        SupervisorSampling owner;CHECK(!owner.start(invalid)&&!owner.start(targets));}
    {Probe p;p.block=true;SupervisorSampling owner;CHECK(owner.start(targets,factory(p))&&!owner.start(targets));
        CHECK(!owner.request({1})&&!owner.request({0,0}));std::vector<uint64_t> generations={9,0};CHECK(owner.request(generations));generations[0]=99;
        CHECK(wait([&]{return p.entered.load();}));const auto began=supervisorNowMs();
        CHECK(owner.pending()&&!owner.take()&&!owner.request({10,1}));CHECK(supervisorNowMs()-began<250);
        p.release=true;std::shared_ptr<const DisplaySampleBatch> sample;CHECK(wait([&]{sample=owner.take();return bool(sample);}));
        CHECK(sample&&sample->generations==std::vector<uint64_t>({9,0})&&sample->revision==1&&sample->readings[0].accepted==7&&!sample->readings[1].stats);
        CHECK(!owner.pending()&&!owner.take()&&p.reads==1);CHECK(owner.request({10,11}));
        CHECK(wait([&]{return p.reads==3;}));CHECK(owner.pending()&&!owner.request({12,13}));
        CHECK(wait([&]{sample=owner.take();return bool(sample);}));CHECK(sample&&sample->revision==2&&sample->generations[0]==10&&sample->generations[1]==11);
        owner.stop();CHECK(!owner.request({12,13})&&!owner.start(targets)&&!owner.take());
        CHECK(p.created==1&&p.destroyed==1&&!p.timedOut&&p.creator==p.reader&&p.reader==p.destroyer&&p.creator!=std::this_thread::get_id());}
    {Probe p;p.block=true;SupervisorSampling owner;CHECK(owner.start(targets,factory(p))&&owner.request({1,1}));CHECK(wait([&]{return p.entered.load();}));
        const auto began=supervisorNowMs();owner.requestStop();CHECK(supervisorNowMs()-began<250&&!owner.pending()&&!owner.take()&&!owner.request({2,2}));
        p.release=true;owner.stop();CHECK(p.reads==1&&p.destroyed==1&&!owner.take()&&!owner.start(targets));}
    {Probe p;p.throws=true;SupervisorSampling owner;CHECK(owner.start(targets,factory(p))&&owner.request({1,1}));CHECK(wait([&]{return owner.faulted();}));
        CHECK(!owner.pending()&&!owner.request({2,2})&&!owner.take());owner.stop();CHECK(p.reads==1&&p.created==1&&p.destroyed==1);}
    {SupervisorSampling owner;CHECK(owner.start(targets,[](const auto &)->std::unique_ptr<DisplaySampleReader>{return {};}));CHECK(owner.request({1,1}));
        CHECK(wait([&]{return owner.faulted();})&&!owner.request({2,2}));}
    {Probe p;SupervisorSampling owner;CHECK(owner.start(targets,[&](const auto &selection){p.entered=true;
            if(!wait([&]{return p.release.load();}))p.timedOut=true;
            return std::make_unique<Reader>(p,selection);}));
        CHECK(owner.request({1,1})&&wait([&]{return p.entered.load();}));
        const auto began=supervisorNowMs();owner.requestStop();CHECK(supervisorNowMs()-began<250&&!owner.pending());
        p.release=true;owner.stop();CHECK(p.created==1&&p.destroyed==1&&p.reads==0&&!p.timedOut&&!owner.take());}
}
void loopback(){
    StatsSnapshot source;source.version="fixture";source.poolState="mining";source.devices.push_back({});source.devices[0].outcomes.accepted=17;
    const auto json=statsToJson(source);StatsServer server;std::string error;std::atomic<unsigned> calls{0};
    CHECK(server.start("127.0.0.1",0,[&]{++calls;return json;},"{}",&error));
    const std::vector<DisplaySampleTarget> nativeTargets={{targets[0].uuid,server.port()}};
    SupervisorSampling owner;CHECK(owner.start(nativeTargets)&&owner.request({5}));
    std::shared_ptr<const DisplaySampleBatch> sample;CHECK(wait([&]{sample=owner.take();return bool(sample);}));
    CHECK(sample&&sample->readings[0].stats&&sample->readings[0].accepted==17&&sample->generations[0]==5);
    if(sample){const auto &r=sample->readings[0];CHECK(displaySampleOrdered(sample->began,r.statsObserved)&&displaySampleOrdered(r.statsObserved,r.powerBegan));
        CHECK(displaySampleOrdered(r.powerBegan,r.powerFinished)&&displaySampleOrdered(r.powerFinished,sample->finished));
        CHECK(!r.telemetry.valid&&!r.telemetry.powerValid);const auto original=r.statsObserved.monotonicMs;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));CHECK(sample->readings[0].statsObserved.monotonicMs==original&&!owner.take()&&calls==1);}
    owner.stop();server.stop();
}
struct FleetState {unsigned ticks=0,stops=0,joins=0;bool revoked=false,throwLines=false;Probe *reader=nullptr;};
class Fleet final:public WorkerFleet {
    FleetState &p;
   public:
    explicit Fleet(FleetState &state):p(state){}
    ~Fleet()override{requestStop();}
    bool start(unsigned,GameGuardTime)override{return true;}
    bool launch(unsigned,std::filesystem::path,std::vector<std::string>,unsigned,GameGuardTime)override{return !p.revoked;}
    void tick(GameGuardTime,const WorkerFleetEvidence &)override{++p.ticks;}
    WorkerFleetState state()const override{return p.revoked?WorkerFleetState::Stopped:WorkerFleetState::Active;}
    WorkerFleetSnapshot snapshot(unsigned)const override{WorkerFleetSnapshot s;s.requested=true;s.done=s.reaped=s.revoked=p.revoked;s.exitCode=p.revoked?0:-1;s.pauseRequested=false;return s;}
    std::vector<std::string> takeLines(unsigned)override{if(p.throwLines&&p.reader&&p.reader->entered)throw 1;return {};}
    void requestStop()override{++p.stops;p.revoked=true;if(p.reader)p.reader->release=true;}
    void stop()override{requestStop();++p.joins;}
};
struct CaptureState {unsigned samples=0,ticks=0,gaps=0,known=0,missing=0;uint64_t last=0;bool backward=false,delayed=false,revoked=false;};
class Capture final:public EnergyCaptureSink {
    CaptureState &p;
   public:
    explicit Capture(CaptureState &state):p(state){}
    bool start(uint64_t,const std::vector<std::string> &)override{return true;}
    EnergyCaptureAdmission submit(uint64_t,const SessionPowerInput &in)override{++p.samples;p.backward=p.backward||in.monotonicMs<p.last;
        if(in.milliwatts)++p.known;else ++p.missing;
        p.delayed=p.delayed||supervisorNowMs()>in.monotonicMs;p.last=in.monotonicMs;return EnergyCaptureAdmission::Buffered;}
    void tick(int64_t,uint64_t mono)override{++p.ticks;p.backward=p.backward||mono<p.last;p.last=mono;}
    void markGap(uint64_t)override{++p.gaps;}
    EnergyHistoryAdmission inspect(uint64_t,uint64_t)override{return EnergyHistoryAdmission::NotReady;}
    EnergyHistoryAdmission retryReviewed(uint64_t,uint64_t)override{return EnergyHistoryAdmission::NotReady;}
    void requestStop()override{p.revoked=true;}
    void stop()override{requestStop();}
    std::shared_ptr<const EnergyCaptureSnapshot> snapshot()const override{return std::make_shared<const EnergyCaptureSnapshot>();}
};
SupervisorOptions options(volatile sig_atomic_t &stop,Probe &reader,FleetState &fleet){
    SupervisorOptions o;o.exe="must-not-be-executed";o.apiEnabled=false;o.stop=&stop;
    for(const auto &t:targets){WorkerSeed seed;seed.uuid=t.uuid;seed.name="fixture.cpu";seed.index=static_cast<int>(o.seeds.size());o.seeds.push_back(seed);}
    o.displaySampleFactory=factory(reader);o.workerFleetFactory=[&](const SessionIntent &,SessionToken){return std::make_unique<Fleet>(fleet);};return o;
}
void supervisor(){
    {volatile sig_atomic_t stop=1;Probe reader;FleetState fleet;Supervisor s(options(stop,reader,fleet));CHECK(s.run()==0&&reader.created==0&&fleet.ticks==0);}
    {volatile sig_atomic_t stop=0;Probe reader;reader.block=true;FleetState fleet;fleet.reader=&reader;auto o=options(stop,reader,fleet);
        unsigned whileBlocked=0;const auto began=supervisorNowMs();o.workerFleetEvidence=[&](uint64_t){
            if(reader.entered&&!reader.release&&++whileBlocked>=4)stop=1;
            if(supervisorNowMs()-began>7000)stop=1;
            return WorkerFleetEvidence{};};
        Supervisor s(std::move(o));CHECK(s.run()==0);CHECK(whileBlocked>=4&&fleet.ticks>=4&&fleet.revoked&&fleet.joins==1);
        CHECK(reader.reads==1&&reader.destroyed==1&&!reader.timedOut&&supervisorNowMs()-began<3000);}
    {volatile sig_atomic_t stop=0;Probe reader;reader.block=true;FleetState fleet;fleet.reader=&reader;fleet.throwLines=true;
        const auto began=supervisorNowMs();bool caught=false;
        try{Supervisor s(options(stop,reader,fleet));s.run();}catch(...){caught=true;}
        CHECK(caught&&fleet.revoked&&reader.destroyed==1&&!reader.timedOut&&supervisorNowMs()-began<3000);}
    {volatile sig_atomic_t stop=0;Probe reader;reader.delayMs=80;FleetState fleet;CaptureState capture;auto o=options(stop,reader,fleet);
        o.energyCaptureFactory=[&]{return std::make_unique<Capture>(capture);};const auto began=supervisorNowMs();
        o.workerFleetEvidence=[&](uint64_t){if(capture.samples>=4||supervisorNowMs()-began>5000)stop=1;return WorkerFleetEvidence{};};
        Supervisor s(std::move(o));CHECK(s.run()==0);CHECK(capture.samples>=4&&capture.ticks>0&&!capture.backward&&capture.delayed&&capture.revoked);
        CHECK(reader.created==1&&reader.destroyed==1&&reader.reads>=4&&capture.known>=4&&capture.missing==0);}
    for(unsigned mode=1;mode<=3;++mode){volatile sig_atomic_t stop=0;Probe reader;reader.powerMode=mode;
        if(mode==3)reader.delayMs=1100;
        FleetState fleet;CaptureState capture;auto o=options(stop,reader,fleet);
        o.energyCaptureFactory=[&]{return std::make_unique<Capture>(capture);};const auto began=supervisorNowMs();
        o.workerFleetEvidence=[&](uint64_t){if(capture.samples>=2||supervisorNowMs()-began>5000)stop=1;return WorkerFleetEvidence{};};
        Supervisor s(std::move(o));CHECK(s.run()==0);CHECK(capture.samples==2&&capture.known==0&&capture.missing==2&&!capture.backward&&capture.revoked);}
}
}
int main(){if(!platformInit())return 2;lifetime();loopback();supervisor();platformShutdown();return finishTests("background supervisor sampling");}
