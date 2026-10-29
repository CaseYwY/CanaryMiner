#include "product/energy_capture.h"
#include "product/energy_history_model.h"
#include "product/energy_recovery.h"
#include "product/energy_review.h"
#include "product/settings.h"
#include "product_test_util.h"
#include <chrono>
#include <fstream>
#include <thread>
using namespace om;
namespace {
constexpr int64_t T=1800000000000LL;
using S=EnergyHistoryStatus;
using A=EnergyCaptureAdmission;
const std::vector<std::string> devices={"fixture.board-a","fixture.board-b"};
const std::string space="0123456789abcdef0123456789abcdef";
EnergyCapturePlan plan(const std::vector<std::string> &ids=devices){
    EnergyCapturePlan p;p.policy.persistenceAuthorized=true;p.policy.bufferedReadings=4;p.policy.batchReadings=2;
    p.privateNamespace=space;p.sessionId="fixture.parent-1";p.mode=EnergyHistoryOpen::CreateNew;p.limits.compaction=EnergyCompactionPolicy{2,32768};
    CHECK(makeEnergyCaptureBinding(space,T,ids,PowerOrigin::SyntheticFixture,&p.binding));return p;
}
SessionPowerInput reading(unsigned channel,uint64_t elapsed,std::optional<uint32_t> power){return {channel,static_cast<uint64_t>(T)+elapsed,elapsed,power};}
template<class P>bool until(P predicate){const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);while(std::chrono::steady_clock::now()<end){if(predicate())return true;std::this_thread::sleep_for(std::chrono::milliseconds(2));}return predicate();}
bool ready(EnergyCaptureSink &c,int64_t wall=T,uint64_t mono=0){return until([&]{c.tick(wall,mono);const auto s=c.snapshot();return s->state==EnergyCaptureState::Collecting&&s->owner->state==EnergyHistoryOwnerState::Ready&&s->buffered==0;});}
bool needsReview(EnergyCaptureSink &c){return until([&]{c.tick(T+5000,5000);return c.snapshot()->state==EnergyCaptureState::NeedsReview;});}
std::string bytes(const std::filesystem::path &p){std::ifstream in(p,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
EnergyHistoryRequest request(const std::filesystem::path &file,const EnergyCapturePlan &p){EnergyHistoryRequest r;r.generation=r.ticket=1;r.file=file;r.binding=p.binding;r.limits=p.limits;r.nowUtcMs=T+100000;r.recovery=EnergyRecoveryMode::Inspect;return r;}
void totals(const std::filesystem::path &file,const EnergyCapturePlan &p,int64_t end,const char *numerator,uint64_t coverage){
    EnergyHistoryLedger ledger;CHECK(ledger.open(file,p.binding,T+100000,EnergyHistoryOpen::Existing,p.limits)==S::Ok);
    const auto t=ledger.query(T,end,T+100000);CHECK(t.status==S::Ok&&t.knownKwh.numerator.text()==numerator&&t.coveredDeviceMs==coverage);
    CHECK(t.requestedDeviceMs==static_cast<uint64_t>(end-T)*p.binding.channels.size());
}
void prepare(const std::filesystem::path &file,const EnergyCapturePlan &p){
    auto r=request(file,p);r.mode=EnergyHistoryOpen::CreateNew;const auto created=executeEnergyHistoryRequest(r);CHECK(created.opened==S::Ok);
    r.mode=EnergyHistoryOpen::Existing;r.recovery=EnergyRecoveryMode::Submit;
    EnergyHistoryBatch b;b.sequence=1;b.id="energy.1";b.sessionId="fixture.old-parent";b.start=EnergyHistorySession{b.sessionId,T,0,5000};b.observedAtMs=T+1000;
    b.readings={{0,T,0,0,100000},{0,T+1000,1000,0,std::nullopt}};r.batch=b;
    EnergyRecoveryJournal journal;CHECK(journal.open(r)==S::Ok&&journal.prepare(r,created.checkpoint)==S::Ok);
}
void layout(const EnergyReviewPanel &p){for(int width:{20,40,60,80,120})for(int rows:{12,16,24,40}){
    const auto text=p.render(width,rows,ColorLevel::None);size_t lines=0,length=0;
    for(char c:text){if(c=='\n'){CHECK(length<static_cast<size_t>(width));length=0;++lines;}else ++length;}
    CHECK(lines<=static_cast<size_t>(rows));
}}
}
int main(int argc,char **argv){
    CHECK(argc==3);if(argc!=3)return finishTests("energy capture");
    const auto helper=std::filesystem::absolute(argv[1]),fixture=std::filesystem::absolute(argv[2]);
    const auto root=std::filesystem::path(makeScratchDir("energy-capture"));const auto clock=[](){return T+100000;};
    // Stable selected-set identity, strict UUIDs and no ordinal/PCI/name fallback.
    auto p=plan();EnergyHistoryBinding binding;
    CHECK(makeEnergyCaptureBinding(space,T,{devices[1],devices[0]},PowerOrigin::SyntheticFixture,&binding));
    CHECK(energy_history_detail::bindingText(binding)==energy_history_detail::bindingText(p.binding));
    for(const auto &ids:std::vector<std::vector<std::string>>{{},{devices[0],devices[0]},{"0"},{"0000:01:00.0"},std::vector<std::string>(65,devices[0])})
        CHECK(!makeEnergyCaptureBinding(space,T,ids,PowerOrigin::ObservedTelemetry,&binding));
    const std::string uuid="GPU-aaaaaaaa-0000-0000-0000-000000000001";
    CHECK(makeEnergyCaptureBinding(space,T,{uuid},PowerOrigin::ObservedTelemetry,&binding));const auto native=binding;
    CHECK(makeEnergyCaptureBinding(space,T,{"GPU-AAAAAAAA-0000-0000-0000-000000000001"},PowerOrigin::ObservedTelemetry,&binding));
    CHECK(binding.channels==native.channels&&binding.evidence.scope==native.evidence.scope);
    CHECK(binding.channels[0].find(uuid)==std::string::npos&&binding.evidence.scope.find(space)==std::string::npos);
    CHECK(makeEnergyCaptureBinding(std::string(32,'a'),T,{uuid},PowerOrigin::ObservedTelemetry,&binding)&&binding.channels!=native.channels);
    CHECK(!makeEnergyCaptureBinding("session-1",T,{uuid},PowerOrigin::ObservedTelemetry,&binding));
    for(unsigned kind=0;kind<9;++kind){auto bad=p;
        if(kind==0)bad.policy.persistenceAuthorized=false;
        if(kind==1)bad.policy.bufferedReadings=129;
        if(kind==2)bad.policy.batchReadings=5;
        if(kind==3)bad.policy.selectionMs=499;
        if(kind==4)bad.policy.maximumGapMs=500;
        if(kind==5)bad.policy.flushMs=0;
        if(kind==6)bad.privateNamespace="";
        if(kind==7)bad.sessionId="session-1";
        if(kind==8)bad.binding.historyBeginUtcMs=0;
        const auto file=root/("invalid-"+std::to_string(kind)+".db");auto c=makeEnergyCapture(helper,file,bad,5000,clock);
        CHECK(!c->start(1,devices)&&!std::filesystem::exists(file));}
    {auto c=makeEnergyCapture(helper,root/"changed.db",p,5000,clock);CHECK(!c->start(1,{devices[0]}));CHECK(!std::filesystem::exists(root/"changed.db"));}
    {auto stopped=makeEnergyCapture(helper,root/"stopped.db",p,5000,clock);stopped->requestStop();CHECK(!stopped->start(1,devices));CHECK(!std::filesystem::exists(root/"stopped.db"));}
    // Overflow is a continuity break after buffered earlier readings, not across them or merely at batch boundaries.
    const auto file=root/"capture.db";auto c=makeEnergyCapture(helper,file,p,5000,clock);CHECK(c->start(1,devices)&&ready(*c));
    CHECK(c->submit(2,reading(0,0,100000))==A::StaleGeneration);
    for(uint64_t t:{0u,1000u})for(unsigned n=0;n<2;++n)CHECK(c->submit(1,reading(n,t,n?200000:100000))==A::Buffered);
    for(unsigned n=0;n<2;++n)CHECK(c->submit(1,reading(n,2000,n?200000:100000))==A::Full);
    CHECK(c->snapshot()->dropped==2&&c->snapshot()->continuity==2&&c->snapshot()->buffered==4);
    CHECK(ready(*c,T+2000,2000));CHECK(c->snapshot()->owner->reply->checkpoint.sequence==2);
    for(uint64_t t:{3000u,4000u}){for(unsigned n=0;n<2;++n)CHECK(c->submit(1,reading(n,t,n?200000:100000))==A::Buffered);CHECK(ready(*c,T+t,t));}
    CHECK(c->snapshot()->owner->reply->checkpoint.compactedSequence==4);
    totals(file,p,T+4000,"600000",4000);
    c->requestStop();const auto stopped=c->snapshot();c->tick(T+10000,10000);CHECK(c->submit(1,reading(0,10000,1))==A::Stopped);
    CHECK(c->snapshot()->revision==stopped->revision&&c->snapshot()->buffered==0);c->stop();CHECK(!c->start(2,devices));
    // Reordered devices preserve mapping; a fresh parent's monotonic zero never fills the old tail.
    auto reordered=p;reordered.mode=EnergyHistoryOpen::Existing;reordered.sessionId="fixture.parent-2";
    c=makeEnergyCapture(helper,file,reordered,5000,clock);CHECK(c->start(1,{devices[1],devices[0]}));CHECK(ready(*c,T+5000,0));
    for(unsigned n=0;n<2;++n)CHECK(c->submit(1,{n,static_cast<uint64_t>(T)+5000,0,n?100000u:200000u})==A::Buffered);
    CHECK(ready(*c,T+5000,0));const auto original=c->snapshot()->owner->reply->recovery->batch;
    EnergyHistoryBinding single;CHECK(makeEnergyCaptureBinding(space,T,{devices[1]},PowerOrigin::SyntheticFixture,&single));
    CHECK(p.binding.channels[original->readings[0].channel]==single.channels[0]);
    for(unsigned n=0;n<2;++n)CHECK(c->submit(1,{n,static_cast<uint64_t>(T)+6000,1000,0})==A::Buffered);
    CHECK(ready(*c,T+6000,1000));CHECK(c->snapshot()->owner->reply->checkpoint.sessionCount==2);c->stop();
    totals(file,p,T+6000,"900000",6000);
    {auto reused=makeEnergyCapture(helper,file,reordered,5000,clock);CHECK(reused->start(1,devices));
        CHECK(until([&]{reused->tick(T+7000,0);return reused->snapshot()->state==EnergyCaptureState::Faulted;}));}
    // Selection skips are intentional sample-and-hold; null, explicit zero and externally marked gaps differ.
    auto one=plan({devices[0]});one.policy.batchReadings=1;const auto oneFile=root/"values.db";
    c=makeEnergyCapture(helper,oneFile,one,5000,clock);CHECK(c->start(1,{devices[0]})&&ready(*c));
    CHECK(c->submit(1,reading(0,0,100000))==A::Buffered&&ready(*c));
    CHECK(c->submit(1,reading(0,500,999999))==A::Skipped);
    CHECK(c->submit(1,reading(0,1000,std::nullopt))==A::Buffered&&ready(*c,T+1000,1000));
    CHECK(c->submit(1,reading(0,2000,0))==A::Buffered&&ready(*c,T+2000,2000));
    CHECK(c->submit(1,reading(0,3000,100000))==A::Buffered&&ready(*c,T+3000,3000));
    c->markGap(2);CHECK(c->snapshot()->continuity==0);c->markGap(1);CHECK(c->snapshot()->continuity==1);
    CHECK(c->submit(1,reading(0,4000,100000))==A::Buffered&&ready(*c,T+4000,4000));
    CHECK(c->submit(1,reading(0,10000,100000))==A::Buffered&&ready(*c,T+10000,10000));c->stop();
    totals(oneFile,one,T+10000,"100000",2000);
    // A failed sensor read between selected observations breaks continuity even though its timestamp is decimated.
    const auto skippedNull=root/"skipped-null.db";c=makeEnergyCapture(helper,skippedNull,one,5000,clock);CHECK(c->start(1,{devices[0]})&&ready(*c));
    CHECK(c->submit(1,reading(0,0,100000))==A::Buffered&&ready(*c));
    CHECK(c->submit(1,reading(0,500,std::nullopt))==A::Skipped&&c->snapshot()->continuity==1);
    CHECK(c->submit(1,reading(0,1000,100000))==A::Buffered&&ready(*c,T+1000,1000));
    CHECK(c->submit(1,reading(0,2000,100000))==A::Buffered&&ready(*c,T+2000,2000));c->stop();
    totals(skippedNull,one,T+2000,"100000",1000);
    // Flush deadline and duplicate input: rejected observations break continuity without consuming a durable sequence.
    auto slow=one;slow.policy.batchReadings=4;const auto slowFile=root/"deadline.db";c=makeEnergyCapture(helper,slowFile,slow,5000,clock);
    CHECK(c->start(1,{devices[0]})&&ready(*c));CHECK(c->submit(1,reading(0,0,100000))==A::Buffered);
    CHECK(c->submit(1,reading(0,0,100000))==A::Invalid);CHECK(c->submit(1,reading(0,1000,100000))==A::Buffered);
    c->tick(T+1999,1999);CHECK(c->snapshot()->buffered==2&&c->snapshot()->owner->reply->checkpoint.sequence==0);
    CHECK(ready(*c,T+2000,2000));CHECK(c->snapshot()->owner->reply->checkpoint.sequence==1&&c->snapshot()->dropped==1);c->stop();
    totals(slowFile,slow,T+2000,"0",0);
    // All 64 channels / 128 readings fit the normal producer and helper envelopes without device-order identity.
    std::vector<std::string> many;for(unsigned n=0;n<64;++n)many.push_back("fixture.board-"+std::to_string(n));
    auto maximum=plan(many);maximum.policy.bufferedReadings=maximum.policy.batchReadings=128;const auto maximumFile=root/"maximum.db";
    c=makeEnergyCapture(helper,maximumFile,maximum,5000,clock);CHECK(c->start(1,many)&&ready(*c));
    for(uint64_t elapsed:{0u,1000u})for(unsigned n=0;n<64;++n)CHECK(c->submit(1,reading(n,elapsed,(n+1)*1000))==A::Buffered);
    CHECK(ready(*c,T+1000,1000));CHECK(c->snapshot()->owner->reply->recovery->batch->readings.size()==128);c->stop();
    totals(maximumFile,maximum,T+1000,"2080000",64000);
    // A definite capacity rejection preserves the committed prefix and discards later unsent observations.
    auto capped=one;capped.policy.batchReadings=2;capped.limits.compaction=EnergyCompactionPolicy{1,1};const auto capFile=root/"capacity.db";
    c=makeEnergyCapture(helper,capFile,capped,5000,clock);CHECK(c->start(1,{devices[0]})&&ready(*c));
    CHECK(c->submit(1,reading(0,0,100000))==A::Buffered);CHECK(c->submit(1,reading(0,1000,200000))==A::Buffered);CHECK(ready(*c,T+1000,1000));
    for(uint64_t t:{2000u,3000u,4000u})CHECK(c->submit(1,reading(0,t,static_cast<uint32_t>(t)*100))==A::Buffered);
    c->tick(T+4000,4000);CHECK(until([&]{c->tick(T+4000,4000);return c->snapshot()->state==EnergyCaptureState::Faulted;}));
    CHECK(c->snapshot()->owner->reply->applied==S::Capacity&&c->snapshot()->buffered==0&&c->snapshot()->dropped==1);
    CHECK(c->submit(1,reading(0,5000,0))==A::Stopped);c->stop();totals(capFile,capped,T+4000,"100000",1000);
    // Latched clock anomalies discard volatile input and forbid recovery/resume in this parent.
    for(unsigned kind=0;kind<4;++kind){auto q=one;q.policy.batchReadings=4;auto test=makeEnergyCapture(helper,root/("clock-"+std::to_string(kind)+".db"),q,5000,clock);
        CHECK(test->start(1,{devices[0]})&&ready(*test));CHECK(test->submit(1,reading(0,1000,1))==A::Buffered);
        if(kind==0)test->tick(T+999,1001);
        if(kind==1)test->tick(T+1001,999);
        if(kind==2)test->tick(T+5001,2000);
        if(kind==3)CHECK(test->submit(1,{0,UINT64_MAX,2000,1})==A::Invalid);
        CHECK(test->snapshot()->state==EnergyCaptureState::ClockFault&&test->snapshot()->buffered==0&&test->snapshot()->dropped>=1);
        test->tick(T+10000,10000);CHECK(test->submit(1,reading(0,10000,0))==A::Stopped);}
    // Exact old Prepared input must be reviewed before admitting a newly identified sampling session.
    const auto pending=root/"pending.db";prepare(pending,p);auto journal=pending;journal+=".energy-recovery.json";const auto before=bytes(journal);
    auto next=p;next.mode=EnergyHistoryOpen::Existing;next.sessionId="fixture.after-review";
    c=makeEnergyCapture(helper,pending,next,5000,clock);CHECK(c->start(1,devices)&&needsReview(*c));
    CHECK(c->submit(1,reading(0,5000,99))==A::NotReady&&bytes(journal)==before);
    EnergyReviewAccess access;access.snapshot=[&]{return c->snapshot();};access.inspect=[&](uint64_t g,uint64_t r){return c->inspect(g,r);};access.retry=[&](uint64_t g,uint64_t r){return c->retryReviewed(g,r);};
    EnergyReviewPanel panel;panel.open(c->snapshot());CHECK(panel.render(80,24,ColorLevel::None).find("fixture.old-parent")==std::string::npos);layout(panel);
    panel.key('r',access);CHECK(panel.reviewing);panel.key(' ',access);panel.key('\r',access);CHECK(!panel.acknowledged&&bytes(journal)==before);
    panel.key('i',access);const auto privateText=panel.render(120,0,ColorLevel::None);CHECK(privateText.find("fixture.old-parent")!=std::string::npos&&privateText.find("MISSING")!=std::string::npos);
    CHECK(privateText.find("100000")!=std::string::npos);layout(panel);
    const auto reviewed=panel.reviewed;CHECK(c->submit(1,reading(0,6000,99))==A::NotReady);panel.update(c->snapshot());CHECK(panel.reviewing); // drops don't invalidate the immutable owner original
    panel.key(' ',access);CHECK(panel.acknowledged);panel.key(27,access);CHECK(!panel.reviewing&&bytes(journal)==before);
    panel.key('r',access);panel.key(' ',access);panel.key('\r',access);CHECK(!panel.reviewing&&panel.submittedTicket);
    CHECK(ready(*c,T+6000,6000));panel.update(c->snapshot());CHECK(!panel.submittedTicket&&panel.message.find("persisted receipt")!=std::string::npos);
    CHECK(c->snapshot()->owner->reply->checkpoint.sequence==1);
    CHECK(c->retryReviewed(1,reviewed->owner->revision)!=EnergyHistoryAdmission::Queued);
    for(unsigned n=0;n<2;++n)CHECK(c->submit(1,reading(n,7000,0))==A::Buffered);
    CHECK(ready(*c,T+7000,7000));CHECK(c->snapshot()->owner->reply->recovery->batch->start->id=="fixture.after-review");c->stop();
    CHECK(c->snapshot()->reconciled&&c->snapshot()->reconciled->reply->checkpoint.sequence==1);
    totals(pending,p,T+7000,"100000",1000);
    // Two concurrent UI callbacks for one reviewed revision admit exactly one retry.
    const auto concurrent=root/"concurrent.db";prepare(concurrent,p);c=makeEnergyCapture(helper,concurrent,next,5000,clock);CHECK(c->start(1,devices)&&needsReview(*c));
    const auto originalRevision=c->snapshot()->owner->revision;EnergyHistoryAdmission first{},second{};
    std::thread a([&]{first=c->retryReviewed(1,originalRevision);}),b([&]{second=c->retryReviewed(1,originalRevision);});a.join();b.join();
    CHECK((first==EnergyHistoryAdmission::Queued)!=(second==EnergyHistoryAdmission::Queued));CHECK(ready(*c,T+5000,5000));c->stop();
    // Stale review invalidation, reveal reset, keys and both native entry points, without an active collector.
    panel.open(reviewed);panel.key('r',access);panel.key('i',access);panel.key(' ',access);
    auto changed=std::make_shared<EnergyCaptureSnapshot>(*reviewed);auto owner=std::make_shared<EnergyHistoryOwnerSnapshot>(*reviewed->owner);++owner->revision;changed->owner=owner;
    panel.update(changed);CHECK(!panel.reviewing&&!panel.acknowledged);panel.key(27,access);CHECK(!panel.visible&&!panel.reveal);
    for(int key:{3,int('q'),int('Q')}){panel.open(reviewed);CHECK(panel.key(key,access)==EnergyReviewResult::Quit);}
    SettingsPanel settings;settings.open({});settings.groupsFocused=false;settings.group=8;settings.field=3;CHECK(settings.fields()==5);
    settings.key('\r',{});CHECK(settings.energyReview.visible&&settings.render(80,24,ColorLevel::None).find("UNAVAILABLE")!=std::string::npos);
    settings.key(27,{});settings.group=2;settings.field=2;CHECK(settings.fields()==4);settings.key('\r',{});CHECK(settings.energyReview.visible);
    // Lost response at a compaction commit is preserved, queued later samples are discarded, and stop cannot publish a late Ready.
    const auto lost=root/"fixture.compact-committed.db";c=makeEnergyCapture(fixture,lost,p,1000,clock);CHECK(c->start(1,devices)&&ready(*c));
    for(unsigned n=0;n<2;++n)CHECK(c->submit(1,reading(n,0,100000))==A::Buffered);
    CHECK(ready(*c));
    for(unsigned n=0;n<2;++n)CHECK(c->submit(1,reading(n,1000,100000))==A::Buffered);
    c->tick(T+1000,1000);
    CHECK(until([&]{c->retryReviewed(1,c->snapshot()->owner->revision);return c->snapshot()->state==EnergyCaptureState::Faulted;}));
    CHECK(static_cast<bool>(c->snapshot()->owner->uncertainBatch)); // a refused UI callback must publish a newly latched fault too
    c->requestStop();const auto rev=c->snapshot()->revision;c->stop();CHECK(c->snapshot()->revision==rev);
    auto recovered=p;recovered.mode=EnergyHistoryOpen::Existing;recovered.sessionId="fixture.reconstructed";c=makeEnergyCapture(helper,lost,recovered,5000,clock);
    CHECK(c->start(1,devices)&&needsReview(*c));CHECK(c->snapshot()->owner->reply->checkpoint.sequence==2&&c->snapshot()->owner->reply->checkpoint.compactedSequence==2);c->stop();
    // A stalled isolated inspection cannot retain an unbounded producer tail or publish Ready after stop.
    const auto stallFile=root/"fixture-energy-stall-open.db";c=makeEnergyCapture(fixture,stallFile,p,5000,clock);CHECK(c->start(1,devices));
    auto marker=stallFile;marker+=".fixture-ready";CHECK(until([&]{return std::filesystem::exists(marker);}));
    for(unsigned n=0;n<256;++n)CHECK(c->submit(1,reading(n%2,n*10,100000))==A::NotReady);
    CHECK(c->snapshot()->dropped==256&&c->snapshot()->buffered==0);
    const auto stopAt=std::chrono::steady_clock::now();c->requestStop();CHECK(std::chrono::steady_clock::now()-stopAt<std::chrono::milliseconds(500));
    const auto stoppedRevision=c->snapshot()->revision;c->stop();c->tick(T+10000,10000);
    CHECK(c->snapshot()->revision==stoppedRevision&&c->snapshot()->owner->creationOutcomeUnknown&&!std::filesystem::exists(stallFile));
    std::filesystem::remove_all(root);return finishTests("energy capture");
}
