#include "energy_history_fixture.h"
#include "product/energy_compaction.h"
#include "product/energy_history_owner.h"
#include "product/hash.h"
#include "product_test_util.h"
#include <chrono>
#include <fstream>
#include <thread>
using namespace om;
using namespace om::energy_history_detail;
using namespace energy_history_fixture;
namespace {
using S=EnergyHistoryStatus;
EnergyHistoryBatch named(EnergyHistoryBatch b){b.id="energy."+std::to_string(b.sequence);return b;}
EnergyHistoryLimits limits(size_t after=2,size_t segments=32768){EnergyHistoryLimits l;l.compaction=EnergyCompactionPolicy{after,segments};return l;}
EnergyHistoryRequest req(const std::filesystem::path &p,std::optional<EnergyHistoryBatch> b={}){auto r=request(p,b);r.limits=limits();r.queries.clear();r.recovery=b?EnergyRecoveryMode::Submit:EnergyRecoveryMode::Inspect;return r;}
std::string bytes(const std::filesystem::path &p){std::ifstream in(p,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
template<class P>bool until(P test){const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);while(std::chrono::steady_clock::now()<end){if(test())return true;std::this_thread::sleep_for(std::chrono::milliseconds(2));}return test();}
bool ready(EnergyHistoryOwner &o){return until([&]{const auto state=o.snapshot()->state;return state==EnergyHistoryOwnerState::Ready||state==EnergyHistoryOwnerState::NeedsReview;});}
void amount(const EnergyHistoryTotals &t,uint64_t mwms,uint64_t covered,uint64_t requested){
    auto text=std::to_string(mwms/1000)+"."+std::to_string(1000+mwms%1000).substr(1);ExactDecimal expected;CHECK(ExactDecimal::parse(text,&expected));
    CHECK(t.status==S::Ok&&t.knownKwh.numerator.compare(expected)==0);CHECK(t.coveredDeviceMs==covered&&t.requestedDeviceMs==requested);
    CHECK(t.completeKwh.has_value()==(covered==requested));
}
}
int main(int argc,char **argv){
    CHECK(argc==3);if(argc!=3)return finishTests("energy compaction");
    const auto helper=std::filesystem::absolute(argv[1]),fixture=std::filesystem::absolute(argv[2]);
    const auto root=std::filesystem::path(makeScratchDir("energy-compaction"));auto l=limits();const auto b= binding();
    CHECK(validEnergyHistoryLimits(l));auto invalid=l;invalid.compaction->afterBatches=0;CHECK(!validEnergyHistoryLimits(invalid));
    invalid=l;invalid.compaction->maxSegments=32769;CHECK(!validEnergyHistoryLimits(invalid));invalid=l;invalid.maxBatches=1;CHECK(!validEnergyHistoryLimits(invalid));
    const auto path=root/"ledger.db";
    {EnergyHistoryLedger ledger;CHECK(ledger.open(path,b,T+20000,EnergyHistoryOpen::CreateNew,l)==S::Ok);
        CHECK(ledger.apply(first(),T+20000)==S::Invalid);CHECK(ledger.apply(named(first()),T+20000)==S::Ok);
        CHECK(ledger.checkpoint(T+20000).compactedSequence==0);CHECK(ledger.apply(named(second()),T+20000)==S::Ok);
        const auto c=ledger.checkpoint(T+20000);CHECK(c.sequence==2&&c.compactedSequence==2&&c.readingCount==8);
        amount(ledger.query(T,T+4000,T+20000),423456000,4000,8000);
        amount(ledger.registerTotal("billing","period-1",T+4000,T+20000),223456000,2500,3000);
        CHECK(ledger.verifyCommitted(named(first()),T+20000)==S::DetailExpired);CHECK(ledger.apply(named(second()),T+20000)==S::Replay);
        auto wrong=named(second());wrong.readings[0].milliwatts=77;CHECK(ledger.verifyCommitted(wrong,T+20000)==S::Conflict);
        auto third=named(second());third.sequence=3;third.id="energy.3";third.observedAtMs=T+4000;third.readings={reading(0,4000,0),reading(1,4000,0)};
        CHECK(ledger.apply(third,T+20000)==S::Ok);amount(ledger.query(T,T+4000,T+20000),425456000,6000,8000);
    }
    const auto saved=bytes(path);
    {EnergyHistoryLedger ledger;CHECK(ledger.open(path,b,T+20000,EnergyHistoryOpen::Existing,l)==S::Ok);CHECK(ledger.checkpoint(T+20000).compactedSequence==2);
        amount(ledger.query(T,T+4000,T+20000),425456000,6000,8000);CHECK(ledger.apply(named(second()),T+20000)==S::Replay);}
    {EnergyHistoryLedger ledger;CHECK(ledger.open(path,b,T+20000)==S::Storage);}
    {EnergyHistoryLedger ledger;auto different=l;different.compaction->afterBatches=3;CHECK(ledger.open(path,b,T+20000,EnergyHistoryOpen::Existing,different)==S::Attribution);}
    CHECK(bytes(path)==saved);
    // The compact index preserves exact millisecond clipping, zero coverage, gaps and asynchronous channel tails.
    {Model model;CHECK(model.advance(b,named(first()),l)==S::Ok);CHECK(compactEnergyModel(b,l,&model)==S::Ok);CHECK(model.intervals.empty()&&model.segments.size()==2);
        EnergyCompactedSnapshot s{model,sha256Hex(bindingText(b)),"",named(first())};s.digest=sha256Hex(s.priorDigest+"|"+batchText(s.original));
        std::string wire;EnergyCompactedSnapshot restored;CHECK(encodeEnergySnapshot(b,l,s,&wire)&&decodeEnergySnapshot(b,l,wire,&restored));
        CHECK(!decodeEnergySnapshot(b,l,wire+" ",&restored));CHECK(!decodeEnergySnapshot(b,l,std::string(energySnapshotLimit+1,' '),&restored));
        CHECK(restored.model.advance(b,named(second()),l)==S::Ok);CHECK(compactEnergyModel(b,l,&restored.model)==S::Ok);
        amount(restored.model.query(b,T+333,T+2789,T+20000),361245152,3123,4912);
        auto bad=model;bad.intervals.push_back({T,T+500,0,1});const auto prior=bad.segments;CHECK(compactEnergyModel(b,l,&bad)==S::Invalid);CHECK(bad.segments.size()==prior.size()&&!bad.intervals.empty());
        auto cap=limits(1,1);CHECK(compactEnergyModel(b,cap,&model)==S::Capacity&&model.segments.size()==2);
    }
    // Independently enumerated held intervals, random power/missing/continuity/session changes and arbitrary query edges.
    struct Expected{int64_t a,z;uint32_t mw;};std::vector<Expected> expected;
    const auto randomFile=root/"random.db";uint32_t rng=123456789;const auto random=[&](){rng=rng*1664525u+1013904223u;return rng;};
    std::optional<uint32_t> previous[2];uint64_t epoch=0;std::string session;uint64_t anchor=0;const unsigned count=401;
    auto randomLimits=limits(7);
    {EnergyHistoryLedger ledger;CHECK(ledger.open(randomFile,b,T+1000000,EnergyHistoryOpen::CreateNew,randomLimits)==S::Ok);
        for(unsigned n=0;n<count;++n){EnergyHistoryBatch batch;batch.sequence=n+1;batch.id="energy."+std::to_string(batch.sequence);batch.observedAtMs=T+1000*n;
            const bool fresh=n%97==0,broken=n%13==0;if(fresh){session="fixture.session-"+std::to_string(n);anchor=n;epoch=0;batch.start=EnergyHistorySession{session,T+1000*n,0,5000};}
            if(broken)++epoch;
            batch.sessionId=session;
            for(unsigned c=0;c<2;++c){std::optional<uint32_t> power;if(random()%7)power=random()%1000000;
                if(n&&!fresh&&!broken&&previous[c])expected.push_back({T+1000*(n-1),T+1000*n,*previous[c]});
                batch.readings.push_back({c,T+1000*n,1000*(n-anchor),epoch,power});previous[c]=power;}
            CHECK(ledger.apply(batch,T+1000000)==S::Ok);
        }
    }
    {EnergyHistoryLedger ledger;CHECK(ledger.open(randomFile,b,T+1000000,EnergyHistoryOpen::Existing,randomLimits)==S::Ok);
        for(unsigned n=0;n<300;++n){const auto a=T-1000+random()%400000,z=a+1+random()%20000;uint64_t energy=0,coverage=0;
            for(const auto &i:expected){const auto from=(std::max)(a,i.a),to=(std::min)(z,i.z);if(to>from){energy+=static_cast<uint64_t>(to-from)*i.mw;coverage+=to-from;}}
            amount(ledger.query(a,z,T+1000000),energy,coverage,2*(z-a));}
    }
    // A 64-day synthetic trace exceeds raw batch/reading budgets while exact constant-power segments stay bounded.
    const auto longFile=root/"long.db";auto longLimits=limits(8);longLimits.maxBatches=8;longLimits.maxReadings=1024;
    const unsigned batches=1441;const int64_t horizon=T+(batches*64LL-1)*60000;EnergyHistoryBatch last;
    {EnergyHistoryLedger ledger;CHECK(ledger.open(longFile,b,horizon,EnergyHistoryOpen::CreateNew,longLimits)==S::Ok);
        for(unsigned n=0;n<batches;++n){EnergyHistoryBatch batch;batch.sequence=n+1;batch.id="energy."+std::to_string(batch.sequence);batch.sessionId="fixture.long-session";
            if(!n)batch.start=EnergyHistorySession{batch.sessionId,T,0,60000};
            for(unsigned j=0;j<64;++j){const uint64_t elapsed=(n*64ULL+j)*60000;for(unsigned c=0;c<2;++c)batch.readings.push_back({c,T+static_cast<int64_t>(elapsed),elapsed,0,c?50000u:100000u});}
            batch.observedAtMs=batch.readings.back().utcMs;CHECK(ledger.apply(batch,horizon)==S::Ok);last=batch;}
        CHECK(ledger.checkpoint(horizon).readingCount==batches*128ULL&&ledger.checkpoint(horizon).compactedSequence==1440);
    }
    {EnergyHistoryLedger ledger;CHECK(ledger.open(longFile,b,horizon,EnergyHistoryOpen::Existing,longLimits)==S::Ok);
        amount(ledger.query(T,horizon,horizon),static_cast<uint64_t>(horizon-T)*150000,2*(horizon-T),2*(horizon-T));
        amount(ledger.query(horizon-86400000+123,horizon-17,horizon),static_cast<uint64_t>(86400000-140)*150000,2*(86400000-140),2*(86400000-140));
        CHECK(ledger.apply(last,horizon)==S::Replay);}
    CHECK(std::filesystem::file_size(longFile)<2*1024*1024);
    // Full-sized valid checkpoint: many structured segments, no oversized JSON string or lost exact zero intervals.
    const auto maximumFile=root/"maximum.db";auto maximumLimits=limits(513);
    {EnergyHistoryLedger ledger;CHECK(ledger.open(maximumFile,b,T+100000,EnergyHistoryOpen::CreateNew,maximumLimits)==S::Ok);
        for(unsigned n=0;n<513;++n){EnergyHistoryBatch batch;batch.sequence=n+1;batch.id="energy."+std::to_string(batch.sequence);batch.sessionId="fixture.maximum";
            if(!n)batch.start=EnergyHistorySession{batch.sessionId,T,0,10};
            for(unsigned j=0;j<(n==512?1u:64u);++j){const uint64_t elapsed=n*64ULL+j;for(unsigned c=0;c<2;++c)batch.readings.push_back({c,T+static_cast<int64_t>(elapsed),elapsed,0,(elapsed%2)?1000u:0u});}
            batch.observedAtMs=batch.readings.back().utcMs;CHECK(ledger.apply(batch,T+100000)==S::Ok);
        }
        CHECK(ledger.checkpoint(T+100000).compactedSequence==513);
    }
    {EnergyHistoryLedger ledger;CHECK(ledger.open(maximumFile,b,T+100000,EnergyHistoryOpen::Existing,maximumLimits)==S::Ok);
        amount(ledger.query(T,T+32768,T+100000),32768000,65536,65536);}
    // Explicit windows/start-only checkpoints preserve absent observation coverage and independent metadata.
    {const auto p=root/"windows-only.db";EnergyHistoryLedger ledger;auto one=limits(1);CHECK(ledger.open(p,b,T+20000,EnergyHistoryOpen::CreateNew,one)==S::Ok);
        auto batch=named(first());batch.start.reset();batch.readings.clear();batch.sessionId.clear();CHECK(ledger.apply(batch,T+20000)==S::Ok);
        amount(ledger.registerTotal("annual-tier","period-1",T+4000,T+20000),0,0,8000);}
    {EnergyHistoryLedger ledger;CHECK(ledger.open(root/"windows-only.db",b,T+20000,EnergyHistoryOpen::Existing,limits(1))==S::Ok);
        auto batch=named(second());batch.start=first().start;batch.readings.clear();CHECK(ledger.apply(batch,T+20000)==S::Ok);
        CHECK(ledger.checkpoint(T+20000).readingCount==0&&ledger.checkpoint(T+20000).sessionCount==1);}
    // Segment-cap exhaustion is atomic and preserves the prior exact facts, rather than downsampling them.
    {EnergyHistoryLedger ledger;auto cap=limits(1,2);CHECK(ledger.open(root/"capacity.db",b,T+20000,EnergyHistoryOpen::CreateNew,cap)==S::Ok);
        CHECK(ledger.apply(named(first()),T+20000)==S::Ok);const auto c=ledger.checkpoint(T+20000);
        CHECK(ledger.apply(named(second()),T+20000)==S::Capacity);CHECK(ledger.checkpoint(T+20000).digest==c.digest);
        amount(ledger.query(T,T+4000,T+20000),323456000,3000,8000);}
    std::atomic<bool> cancel{false};EnergyHistoryProcessClient client(helper),fake(fixture);
    auto r=req(root/"wire.db",named(first()));r.mode=EnergyHistoryOpen::CreateNew;auto result=client.transact(r,cancel,5000);
    CHECK(result.reply&&result.reply->applied==S::Ok);r.mode=EnergyHistoryOpen::Existing;r.batch=named(second());result=client.transact(r,cancel,5000);
    CHECK(result.reply&&result.reply->applied==S::Ok&&result.reply->checkpoint.compactedSequence==2);
    std::string wire;EnergyHistoryReply parsed;CHECK(encodeEnergyHistoryReply(r,*result.reply,&wire));auto v2=r;v2.limits.compaction.reset();CHECK(!decodeEnergyHistoryReply(v2,wire,&parsed));
    for(unsigned kind=0;kind<4;++kind){auto forged=*result.reply;
        if(kind==0)forged.checkpoint.compactedSequence=0;
        if(kind==1)forged.checkpoint.compactedSequence=3;
        if(kind==2)forged.checkpoint.readingCount=257;
        if(kind==3)forged.checkpoint.digest=std::string(64,'0');
        CHECK(!encodeEnergyHistoryReply(r,forged,&wire));}
    r.batch=named(first());r.recovery=EnergyRecoveryMode::Volatile;result=client.transact(r,cancel,5000);CHECK(result.reply&&result.reply->applied==S::DetailExpired&&!result.writeOutcomeUnknown);
    for(const auto &phase:std::vector<std::string>{"prepared","committed","bad-ack"}){
        const auto p=root/("fixture.compact-"+phase+".db");r=req(p,named(first()));r.mode=EnergyHistoryOpen::CreateNew;CHECK(client.transact(r,cancel,5000).reply->applied==S::Ok);
        r.mode=EnergyHistoryOpen::Existing;r.batch=named(second());result=fake.transact(r,cancel,1000);CHECK(!result.reply&&result.writeOutcomeUnknown);
        EnergyHistoryOwner owner(helper,p,b,l,5000,[]{return T+20000;});CHECK(owner.start(1)&&ready(owner));const auto s=owner.snapshot();
        CHECK(s->reply->checkpoint.compactedSequence==(phase=="prepared"?0u:2u));
        if(phase!="bad-ack"){CHECK(s->state==EnergyHistoryOwnerState::NeedsReview);CHECK(owner.retryReviewed(1,s->revision)==EnergyHistoryAdmission::Queued&&ready(owner));}
        CHECK(owner.snapshot()->state==EnergyHistoryOwnerState::Ready&&owner.snapshot()->reply->checkpoint.sequence==2);owner.stop();
    }
    std::filesystem::remove_all(root);return finishTests("energy compaction");
}
