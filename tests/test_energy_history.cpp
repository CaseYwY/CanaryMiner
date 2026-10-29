#include "energy_history_fixture.h"
#include "product/energy_history_owner.h"
#include "product/energy_history_model.h"
#include "product_test_util.h"
#include <chrono>
#include <fstream>
#include <thread>
using namespace om;
using namespace energy_history_fixture;
namespace {
using S=EnergyHistoryStatus;
template<class P>bool until(P predicate){const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);while(std::chrono::steady_clock::now()<end){if(predicate())return true;std::this_thread::sleep_for(std::chrono::milliseconds(2));}return predicate();}
bool ready(EnergyHistoryOwner &o){return until([&]{const auto s=o.snapshot()->state;return s==EnergyHistoryOwnerState::Ready||s==EnergyHistoryOwnerState::NeedsReview;});}
std::string bytes(const std::filesystem::path &p){std::ifstream in(p,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
void total(const EnergyHistoryTotals &t,const std::string &n,uint64_t covered,uint64_t requested){
    CHECK(t.status==S::Ok);CHECK(t.knownKwh.numerator.text()==n);CHECK(t.coveredDeviceMs==covered&&t.requestedDeviceMs==requested);
    const auto expected=covered==requested?EnergyHistoryCoverage::Complete:covered?EnergyHistoryCoverage::Partial:EnergyHistoryCoverage::Unavailable;
    CHECK(t.coverage==expected&&t.completeKwh.has_value()==(expected==EnergyHistoryCoverage::Complete));
}
void checkpoint(EnergyHistoryLedger &l,uint64_t seq,uint64_t readings,uint64_t sessions=1){const auto c=l.checkpoint(T+20000);CHECK(c.status==S::Ok&&c.sequence==seq&&c.readingCount==readings&&c.sessionCount==sessions);}
}
int main(int argc,char **argv){
    CHECK(argc==3);if(argc!=3)return finishTests("energy history");
    const auto helper=std::filesystem::absolute(argv[1]),fixture=std::filesystem::absolute(argv[2]);
    const auto root=std::filesystem::path(makeScratchDir("energy-history"));
    CHECK(validEnergyHistoryBinding(binding()));
    auto bound=binding();bound.evidence.origin=PowerOrigin::DeclaredForecast;CHECK(!validEnergyHistoryBinding(bound));
    bound=binding();bound.channels.push_back(bound.channels[0]);CHECK(!validEnergyHistoryBinding(bound));
    bound=binding();bound.evidence.origin=PowerOrigin::ObservedTelemetry;CHECK(!validEnergyHistoryBinding(bound));
    bound.evidence.adapter="native-nvml-milliwatts";CHECK(validEnergyHistoryBinding(bound));bound.evidence.basis=PowerBasis::WholeSystemWall;CHECK(!validEnergyHistoryBinding(bound));
    for(const auto &id:std::vector<std::string>{"",std::string(129,'a'),"line\nbreak","space id","\xc3\xa9"}){auto b=binding();b.channels[0]=id;CHECK(!validEnergyHistoryBinding(b));}
    {
        EnergyHistoryLedger l;CHECK(l.open(root/"ledger.db",binding(),T+20000,EnergyHistoryOpen::CreateNew)==S::Ok);
        checkpoint(l,0,0,0);total(l.query(T,T+4000,T+20000),"0",0,8000);
        EnergyHistoryLedger contender;CHECK(contender.open(root/"ledger.db",binding(),T+20000)==S::Storage);
        CHECK(l.apply(first(),T+20000)==S::Ok);checkpoint(l,1,6);
        total(l.query(T,T+2000,T+20000),"323456",3000,4000);
        total(l.query(T,T+1000,T+20000),"123456",2000,2000); // valid zero on channel B IS covered
        total(l.query(T+1000,T+2000,T+20000),"200000",1000,2000); // missing B is not covered zero
        total(l.query(T+2000,T+4000,T+20000),"0",0,4000); // no tail extrapolation
        total(l.query(T-1000,T+4000,T+20000),"323456",3000,10000); // prehistory unavailable
        total(l.registerTotal("billing","period-1",T+4000,T+20000),"223456",2500,3000);
        total(l.registerTotal("billing","period-2",T+4000,T+20000),"100000",500,5000);
        total(l.registerTotal("annual-tier","period-1",T+4000,T+20000),"323456",3000,8000);
        CHECK(l.apply(first(),T+20000)==S::Replay);checkpoint(l,1,6);
        auto changed=first();changed.observedAtMs++;CHECK(l.apply(changed,T+20000)==S::Conflict); // retry never renews observation time
        changed=first();changed.readings[0].milliwatts=1;CHECK(l.apply(changed,T+20000)==S::Conflict);
        auto bad=second();bad.sequence=3;CHECK(l.apply(bad,T+20000)==S::SequenceGap);
        bad=second();bad.id=first().id;CHECK(l.apply(bad,T+20000)==S::Conflict);
        bad=second();bad.sessionId="other-session";CHECK(l.apply(bad,T+20000)==S::Conflict);
        bad=second();bad.readings[1].monotonicMs=100;CHECK(l.apply(bad,T+20000)==S::Clock);checkpoint(l,1,6); // entire batch atomic, even after a valid reading
        total(l.query(T,T+4000,T+20000),"323456",3000,8000);
        CHECK(l.apply(second(),T+20000)==S::Ok);checkpoint(l,2,8);
        total(l.query(T,T+4000,T+20000),"423456",4000,8000);
        CHECK(l.apply(first(),T+20000)==S::Replay);CHECK(l.checkpoint(T+20000).observedAtMs==second().observedAtMs);
        CHECK(l.query(T,T+4000,T+1000).status==S::Invalid);
        CHECK(l.query(T,T+1000,T+2000).status==S::Clock);
        CHECK(l.checkpoint(T+2000).status==S::Clock);
        CHECK(l.registerTotal("absent","period",T+4000,T+20000).status==S::Invalid);
        auto next=second();next.sequence=3;next.id="fixture.new-session";next.sessionId="fixture.parent-2";next.start=EnergyHistorySession{next.sessionId,T+5000,0,5000};next.observedAtMs=T+6000;
        next.readings={{0,T+5000,0,0,1000},{1,T+5000,0,0,1000},{0,T+6000,1000,0,0},{1,T+6000,1000,0,0}};
        CHECK(l.apply(next,T+20000)==S::Ok);checkpoint(l,3,12,2);
        total(l.query(T,T+10000,T+20000),"425456",6000,20000); // no cross-session sample hold
        auto reused=next;reused.sequence=4;reused.id="fixture.reuse";reused.start->anchorUtcMs=T+7000;reused.readings.clear();CHECK(l.apply(reused,T+20000)==S::Invalid);
        reused.observedAtMs=T+7000;CHECK(l.apply(reused,T+20000)==S::Conflict);
    }
    const auto original=bytes(root/"ledger.db");
    {EnergyHistoryLedger l;CHECK(l.open(root/"ledger.db",binding(),T+20000)==S::Ok);checkpoint(l,3,12,2);total(l.query(T,T+10000,T+20000),"425456",6000,20000);}
    {EnergyHistoryLedger l;CHECK(l.open(root/"ledger.db",binding(),T+20000,EnergyHistoryOpen::CreateNew)==S::Conflict);}
    {EnergyHistoryLedger l;CHECK(l.open(root/"missing.db",binding(),T+20000)==S::Storage);CHECK(!std::filesystem::exists(root/"missing.db"));}
    {EnergyHistoryLedger l;auto b=binding();b.channels[0]="fixture.other";CHECK(l.open(root/"ledger.db",b,T+20000)==S::Attribution);}
    {EnergyHistoryLedger l;CHECK(l.open(root/"ledger.db",binding(),T+1000)==S::Clock);}
    CHECK(bytes(root/"ledger.db")==original);
    // Strict per-reading clocks, explicit discontinuity and max-gap behavior; rejecting input preserves prior facts.
    for(unsigned kind=0;kind<9;++kind){
        EnergyHistoryLedger l;CHECK(l.open(root/("guard-"+std::to_string(kind)+".db"),binding(),T+20000,EnergyHistoryOpen::CreateNew)==S::Ok);
        CHECK(l.apply(first(),T+20000)==S::Ok);auto b=second();
        if(kind==0)b.readings[0].monotonicMs=2100; // duplicate board time
        if(kind==1)b.readings[0].utcMs=T+1999; // wall rollback
        if(kind==2)b.readings[0].utcMs=T+6001; // drift > guard
        if(kind==3)b.readings[0].monotonicMs=UINT64_MAX;
        if(kind==4)b.readings[0].channel=2;
        if(kind==5)b.readings[0].utcMs=0;
        if(kind==6)b.start=EnergyHistorySession{b.sessionId,T+3000,0,5000}; // reused session
        if(kind==7){b.readings[0].continuity=1;b.readings[1].continuity=0;}
        if(kind==8){b.start=EnergyHistorySession{"fixture.new",T+1999,0,5000};b.sessionId=b.start->id;b.readings.clear();}
        CHECK(l.apply(b,T+20000)!=S::Ok);checkpoint(l,1,6);total(l.query(T,T+4000,T+20000),"323456",3000,8000);
    }
    {
        EnergyHistoryLedger l;CHECK(l.open(root/"gaps.db",binding(),T+20000,EnergyHistoryOpen::CreateNew)==S::Ok);CHECK(l.apply(first(),T+20000)==S::Ok);
        auto b=second();for(auto &r:b.readings)r.continuity=1;CHECK(l.apply(b,T+20000)==S::Ok);total(l.query(T,T+4000,T+20000),"323456",3000,8000);
        b.sequence=3;b.id="fixture.gap";b.observedAtMs=T+9000;b.readings={reading(0,9000,0,1),reading(1,9000,0,1)};
        CHECK(l.apply(b,T+20000)==S::Ok);total(l.query(T,T+10000,T+20000),"323456",3000,20000);
        b.sequence=4;b.id="fixture.zero";b.observedAtMs=T+10000;b.readings={reading(0,10000,0,1),reading(1,10000,0,1)};
        CHECK(l.apply(b,T+20000)==S::Ok);total(l.query(T+9000,T+10000,T+20000),"0",2000,2000);
    }
    // Independent reset windows do not zero accumulated history, infer a tariff or accept overlaps in one register.
    for(unsigned kind=0;kind<8;++kind){EnergyHistoryLedger l;CHECK(l.open(root/("window-"+std::to_string(kind)+".db"),binding(),T+20000,EnergyHistoryOpen::CreateNew)==S::Ok);
        CHECK(l.apply(first(),T+20000)==S::Ok);auto b=second();b.readings.clear();b.sessionId.clear();b.windows={{"billing","period-3","rule-2",T+4000,T+10000,false,0}};
        if(kind==0)b.windows[0].beginUtcMs=T+3999;
        if(kind==1)b.windows[0].periodId="period-1";
        if(kind==2)b.windows[0].endUtcMs=b.windows[0].beginUtcMs;
        if(kind==3)b.windows[0].endUtcMs=b.windows[0].beginUtcMs+371LL*86400000;
        if(kind==4)b.windows[0].offsetSeconds=3600;
        if(kind==5){b.windows[0].fixedStandardTime=true;b.windows[0].offsetSeconds=1;}
        if(kind==6)b.windows.push_back(b.windows[0]);
        CHECK(l.apply(b,T+20000)==(kind==7?S::Ok:kind<2||kind==6?S::Conflict:S::Invalid));
        total(l.query(T,T+10000,T+20000),"323456",3000,20000);
        if(kind==7)total(l.registerTotal("billing","period-3",T+10000,T+20000),"0",0,12000);
    }
    for(unsigned kind=0;kind<4;++kind){EnergyHistoryLimits limits;if(kind==0)limits.maxBatches=1;if(kind==1)limits.maxReadings=6;if(kind==2)limits.maxSessions=1;if(kind==3)limits.maxWindows=3;
        EnergyHistoryLedger l;CHECK(l.open(root/("capacity-"+std::to_string(kind)+".db"),binding(),T+20000,EnergyHistoryOpen::CreateNew,limits)==S::Ok);CHECK(l.apply(first(),T+20000)==S::Ok);
        auto b=second();if(kind==2){b.start=EnergyHistorySession{"fixture.next",T+3000,0,5000};b.sessionId=b.start->id;b.readings.clear();}
        if(kind==3)b.windows={{"other","period-1","rule-1",T,T+3000,false,0}};
        CHECK(l.apply(b,T+20000)==S::Capacity);checkpoint(l,1,6);
    }
    // Milliwatt precision and clipping at every millisecond boundary, checked against integer arithmetic.
    {EnergyHistoryLedger l;auto b=first();b.readings={reading(0,0,UINT32_MAX),reading(1,0,1),reading(0,1000,0),reading(1,1000,0)};
        CHECK(l.open(root/"precision.db",binding(),T+20000,EnergyHistoryOpen::CreateNew)==S::Ok);CHECK(l.apply(b,T+20000)==S::Ok);
        for(unsigned dt=1;dt<=1000;dt+=13){const auto t=l.query(T+3,T+3+dt,T+20000);const auto length=(std::min)(dt,997u);
            const uint64_t milli=4294967296ULL*length;auto expected=std::to_string(milli/1000)+"."+std::to_string(1000+milli%1000).substr(1);ExactDecimal d;CHECK(ExactDecimal::parse(expected,&d));
            total(t,d.text(),2*length,2*dt);}
    }
    auto r=request(root/"wire.db",first());r.mode=EnergyHistoryOpen::CreateNew;std::string wire;EnergyHistoryRequest decoded;EnergyHistoryReply parsed;
    CHECK(encodeEnergyHistoryRequest(r,&wire)&&decodeEnergyHistoryRequest(wire,&decoded));
    CHECK(energy_history_detail::batchText(*decoded.batch)==energy_history_detail::batchText(*r.batch));
    CHECK(!decodeEnergyHistoryRequest(wire+"x",&decoded));CHECK(!decodeEnergyHistoryRequest(std::string(65537,' '),&decoded));
    {
        auto large=request(root/"maximum.db",first());large.mode=EnergyHistoryOpen::CreateNew;large.binding.channels.clear();large.batch->readings.clear();large.batch->windows.clear();
        for(unsigned i=0;i<64;++i){large.binding.channels.push_back("fixture.channel-"+std::string(110,'x')+std::to_string(i));large.batch->readings.push_back(reading(i,0,UINT32_MAX));}
        for(unsigned i=0;i<64;++i)large.batch->readings.push_back(reading(i,1000,0));
        for(unsigned i=0;i<32;++i)large.batch->windows.push_back({"fixture.register-"+std::string(107,'x')+std::to_string(i),std::string(128,'p'),std::string(128,'r'),T,T+4000,false,0});
        large.queries={{T,T+4000,"",""}};
        CHECK(encodeEnergyHistoryRequest(large,&wire)&&wire.size()>20000&&decodeEnergyHistoryRequest(wire,&decoded));
        CHECK(decoded.binding.channels.size()==64&&decoded.batch->readings.size()==128&&decoded.batch->windows.size()==32);
        auto extra=large;extra.batch->readings.push_back(reading(0,2000,0));CHECK(!encodeEnergyHistoryRequest(extra,&wire));
        const auto saved=executeEnergyHistoryRequest(large);CHECK(saved.applied==S::Ok&&saved.checkpoint.readingCount==128);
        total(saved.totals[0],"274877906880",64000,256000);
        CHECK(encodeEnergyHistoryReply(large,saved,&wire)&&decodeEnergyHistoryReply(large,wire,&parsed));
        EnergyHistoryProcessClient boundary(helper);std::atomic<bool> cancelLarge{false};large.mode=EnergyHistoryOpen::Existing;
        const auto throughHelper=boundary.transact(large,cancelLarge,5000);
        CHECK(throughHelper.transport==LocalTaskStatus::Ok&&throughHelper.reply&&throughHelper.reply->applied==S::Replay);
    }
    const auto reply=executeEnergyHistoryRequest(r);CHECK(reply.applied==S::Ok&&reply.checkpoint.sequence==1&&reply.totals.size()==4);
    CHECK(encodeEnergyHistoryReply(r,reply,&wire)&&decodeEnergyHistoryReply(r,wire,&parsed));
    for(unsigned kind=0;kind<14;++kind){auto forged=reply;
        if(kind==0)++forged.generation;
        if(kind==1)++forged.ticket;
        if(kind==2)++forged.checkpoint.sequence;
        if(kind==3)forged.checkpoint.batchId="wrong";
        if(kind==4)forged.checkpoint.status=S::Storage;
        if(kind==5)forged.totals[0].coveredDeviceMs+=10000;
        if(kind==6)forged.totals[0].completeKwh=forged.totals[0].knownKwh;
        if(kind==7)forged.totals[0].observedAtMs++;
        if(kind==8)forged.applied.reset();
        if(kind==9)ExactDecimal::parse("999999999999999999999999",&forged.totals[0].knownKwh.numerator);
        if(kind==10)ExactDecimal::parse("0.0001",&forged.totals[0].knownKwh.numerator);
        if(kind==11)forged.checkpoint.sessionCount=2;
        if(kind==12)forged.checkpoint.throughUtcMs=T;
        if(kind==13)forged.checkpoint.readingCount=0;
        CHECK(!encodeEnergyHistoryReply(r,forged,&wire));}
    r.mode=EnergyHistoryOpen::Existing;CHECK(executeEnergyHistoryRequest(r).applied==S::Replay);
    r.batch.reset();r.queries[1].beginUtcMs++;CHECK(executeEnergyHistoryRequest(r).totals[1].status==S::Invalid);
    // Native process boundary, uncertain post-commit acknowledgement, explicit inspect and exact retry.
    std::atomic<bool> cancel{false};EnergyHistoryProcessClient client(helper),fake(fixture);
    r=request(root/"process.db",first());r.mode=EnergyHistoryOpen::CreateNew;
    auto result=client.transact(r,cancel,5000);CHECK(result.transport==LocalTaskStatus::Ok&&!result.writeOutcomeUnknown&&result.reply->applied==S::Ok);
    r.mode=EnergyHistoryOpen::Existing;r.batch=second();r.batch->id="fixture.stall-after";
    result=fake.transact(r,cancel,200);CHECK(result.transport==LocalTaskStatus::Timeout&&result.writeOutcomeUnknown&&!result.reply);
    auto inspect=r;inspect.batch.reset();result=client.transact(inspect,cancel,5000);CHECK(result.reply&&result.reply->checkpoint.sequence==2&&result.reply->totals[0].knownKwh.numerator.text()=="423456");
    result=client.transact(r,cancel,5000);CHECK(result.reply&&result.reply->applied==S::Replay&&!result.writeOutcomeUnknown);
    cancel=true;CHECK(client.transact(r,cancel,5000).transport==LocalTaskStatus::Cancelled);cancel=false;
    {
        EnergyHistoryOwner owner(helper,root/"owner.db",binding(),{},5000,[]{return T+20000;});
        CHECK(owner.start(1,EnergyHistoryOpen::CreateNew)&&ready(owner));const auto observed=owner.snapshot();
        CHECK(owner.submit(2,observed->revision,first())==EnergyHistoryAdmission::StaleGeneration);
        CHECK(owner.submit(1,observed->revision+1,first())==EnergyHistoryAdmission::ReviewChanged);
        CHECK(owner.refresh(1,observed->revision,{{T,T+4000,std::string(129,'x'),"period"}})==EnergyHistoryAdmission::Invalid);
        EnergyHistoryAdmission a{},b{};std::thread one([&]{a=owner.submit(1,observed->revision,first());}),two([&]{b=owner.submit(1,observed->revision,first());});one.join();two.join();
        CHECK((a==EnergyHistoryAdmission::Queued)!=(b==EnergyHistoryAdmission::Queued));CHECK(ready(owner));
        CHECK(owner.snapshot()->reply->checkpoint.sequence==1&&observed->reply->checkpoint.sequence==0);
        CHECK(owner.submit(1,owner.snapshot()->revision,second(),request(root/"unused").queries)==EnergyHistoryAdmission::Queued&&ready(owner));
        CHECK(owner.snapshot()->reply->totals[0].knownKwh.numerator.text()=="423456");owner.stop();
        CHECK(!owner.start(1));CHECK(owner.start(2)&&ready(owner));auto next=second();next.sequence=3;next.id="fixture.no-implicit-resume";next.observedAtMs=T+4000;next.readings={reading(0,4000,0),reading(1,4000,0)};
        CHECK(owner.submit(2,owner.snapshot()->revision,next)==EnergyHistoryAdmission::Invalid);owner.stop();
    }
    for(const auto &id:std::vector<std::string>{"fixture.stall-before","fixture.stall-after","fixture.bad-ack"}){
        const auto path=root/(id+".db");EnergyHistoryOwner owner(fixture,path,binding(),{},300,[]{return T+20000;});CHECK(owner.start(1,EnergyHistoryOpen::CreateNew)&&ready(owner));
        auto batch=first();batch.id=id;CHECK(owner.submit(1,owner.snapshot()->revision,batch)==EnergyHistoryAdmission::Queued);
        CHECK(until([&]{return owner.snapshot()->state==EnergyHistoryOwnerState::Faulted;}));CHECK(owner.snapshot()->uncertainBatch&&owner.snapshot()->uncertainBatch->id==id);
        const auto retained=owner.snapshot()->uncertainBatch;owner.stop();CHECK(owner.start(2)&&ready(owner));
        const auto s=owner.snapshot();CHECK(s->state==EnergyHistoryOwnerState::NeedsReview&&s->uncertainBatch==retained);
        CHECK(s->reply->checkpoint.sequence==(id=="fixture.stall-before"?0u:1u));
        CHECK(owner.submit(2,s->revision,second())==EnergyHistoryAdmission::NotReady);
        CHECK(owner.retryReviewed(2,s->revision-1)==EnergyHistoryAdmission::ReviewChanged);
        if(id=="fixture.stall-after"){CHECK(owner.retryReviewed(2,s->revision)==EnergyHistoryAdmission::Queued&&ready(owner));CHECK(!owner.snapshot()->uncertainBatch&&owner.snapshot()->reply->applied==S::Replay);}
        owner.stop();
    }
    {
        const auto path=root/"stop-inflight.db";EnergyHistoryOwner owner(fixture,path,binding(),{},5000,[]{return T+20000;});
        CHECK(owner.start(1,EnergyHistoryOpen::CreateNew)&&ready(owner));auto b=first();b.id="fixture.stall-after";
        CHECK(owner.submit(1,owner.snapshot()->revision,b)==EnergyHistoryAdmission::Queued);auto marker=path;marker+=".fixture-ready";
        CHECK(until([&]{return std::filesystem::exists(marker);}));const auto start=std::chrono::steady_clock::now();owner.requestStop();const auto stopped=owner.snapshot();
        CHECK(std::chrono::steady_clock::now()-start<std::chrono::milliseconds(100));CHECK(stopped->state==EnergyHistoryOwnerState::Stopped&&stopped->uncertainBatch);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));CHECK(owner.snapshot()==stopped); // cancelled completion cannot publish/revive
        CHECK(owner.submit(1,stopped->revision,second())==EnergyHistoryAdmission::NotReady);owner.stop();
        auto read=request(path);read.batch.reset();const auto saved=client.transact(read,cancel,5000);CHECK(saved.reply&&saved.reply->checkpoint.sequence==1); // stop does not roll back COMMIT
        CHECK(owner.start(2)&&ready(owner));CHECK(owner.snapshot()->state==EnergyHistoryOwnerState::NeedsReview);
        CHECK(owner.retryReviewed(2,owner.snapshot()->revision)==EnergyHistoryAdmission::Queued&&ready(owner));CHECK(owner.snapshot()->reply->applied==S::Replay);
        owner.stop();
    }
    {
        const auto path=root/"fixture-energy-stall-open.db";EnergyHistoryOwner owner(fixture,path,binding(),{},5000,[]{return T+20000;});
        CHECK(owner.start(1,EnergyHistoryOpen::CreateNew));auto marker=path;marker+=".fixture-ready";CHECK(until([&]{return std::filesystem::exists(marker);}));
        const auto start=std::chrono::steady_clock::now();owner.requestStop();const auto stopped=owner.snapshot();
        CHECK(std::chrono::steady_clock::now()-start<std::chrono::milliseconds(100));CHECK(stopped->state==EnergyHistoryOwnerState::Stopped&&stopped->creationOutcomeUnknown);
        owner.stop();CHECK(!std::filesystem::exists(path));CHECK(!owner.start(2,EnergyHistoryOpen::CreateNew));
    }
    std::filesystem::remove_all(root);return finishTests("energy history");
}
