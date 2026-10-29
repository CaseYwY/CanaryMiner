#include "energy_history_fixture.h"
#include "product/energy_history_model.h"
#include "product/energy_history_owner.h"
#include "product/energy_recovery.h"
#include "product/hash.h"
#include "product_test_util.h"
#include <chrono>
#include <fstream>
#include <thread>
using namespace om;
using namespace energy_history_fixture;
namespace {
using S=EnergyHistoryStatus;
using R=EnergyRecoveryState;
using M=EnergyRecoveryMode;
std::string bytes(const std::filesystem::path &p){std::ifstream in(p,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
void write(const std::filesystem::path &p,const std::string &s){std::ofstream out(p,std::ios::binary|std::ios::trunc);out<<s;}
std::filesystem::path sidecar(std::filesystem::path p){p+=".energy-recovery.json";return p;}
EnergyHistoryRequest req(const std::filesystem::path &p,std::optional<EnergyHistoryBatch> b={}){
    auto r=request(p,b);r.queries.clear();r.recovery=b?M::Submit:M::Inspect;return r;
}
EnergyHistoryReply execute(const EnergyHistoryRequest &r){
    std::string wire;EnergyHistoryRequest request;EnergyHistoryReply parsed;
    CHECK(encodeEnergyHistoryRequest(r,&wire)&&decodeEnergyHistoryRequest(wire,&request));
    auto reply=executeEnergyHistoryRequest(request);
    CHECK(encodeEnergyHistoryReply(r,reply,&wire)&&decodeEnergyHistoryReply(r,wire,&parsed));return reply;
}
void create(const std::filesystem::path &p){auto r=req(p);r.mode=EnergyHistoryOpen::CreateNew;const auto a=execute(r);
    CHECK(a.opened==S::Ok&&a.recovery&&a.recovery->state==R::Empty&&a.checkpoint.sequence==0);}
template<class P>bool until(P predicate){const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);while(std::chrono::steady_clock::now()<end){if(predicate())return true;std::this_thread::sleep_for(std::chrono::milliseconds(2));}return predicate();}
bool ready(EnergyHistoryOwner &o){return until([&]{return o.snapshot()->state==EnergyHistoryOwnerState::Ready||o.snapshot()->state==EnergyHistoryOwnerState::NeedsReview;});}
void prepared(const EnergyHistoryRequest &r,bool commit=false){
    EnergyRecoveryJournal j;CHECK(j.open(r)==S::Ok);EnergyHistoryLedger l;CHECK(l.open(r.file,r.binding,r.nowUtcMs)==S::Ok);
    CHECK(j.prepare(r,l.checkpoint(r.nowUtcMs))==S::Ok);CHECK(j.snapshot().state==R::Prepared&&j.matches(r));
    CHECK(j.prepare(r,l.checkpoint(r.nowUtcMs))==S::Conflict);
    CHECK(j.settle(S::Storage)==S::Invalid);if(commit)CHECK(l.apply(*r.batch,r.nowUtcMs)==S::Ok);
}
}
int main(int argc,char **argv){
    CHECK(argc==3);if(argc!=3)return finishTests("energy recovery");
    const auto helper=std::filesystem::absolute(argv[1]),fixture=std::filesystem::absolute(argv[2]);
    const auto root=std::filesystem::path(makeScratchDir("energy-recovery"));std::atomic<bool> cancel{false};
    EnergyHistoryProcessClient client(helper),fake(fixture);
    // Explicit creation and strict mode/version boundaries. Existing never invents an empty sidecar.
    const auto file=root/"modes.db";auto r=req(file);CHECK(execute(r).opened==S::Storage&&!std::filesystem::exists(file));create(file);
    const auto emptyJournal=bytes(sidecar(file)),emptyDb=bytes(file);
    auto again=r;again.mode=EnergyHistoryOpen::CreateNew;CHECK(execute(again).opened==S::Conflict);
    CHECK(bytes(file)==emptyDb&&bytes(sidecar(file))==emptyJournal);
    std::string wire;EnergyHistoryRequest decoded;
    CHECK(encodeEnergyHistoryRequest(r,&wire)&&wire.find("CMEQ2")!=std::string::npos);
    for(unsigned kind=0;kind<5;++kind){auto bad=r;
        if(kind==0)bad.batch=first();
        if(kind==1)bad.recovery=M::Submit;
        if(kind==2)bad.recovery=M::Retry;
        if(kind==3)bad.recovery=static_cast<M>(4);
        if(kind==4){bad.recovery=M::Retry;bad.batch=first();bad.mode=EnergyHistoryOpen::CreateNew;}
        CHECK(!encodeEnergyHistoryRequest(bad,&wire));}
    r=req(file,first());auto reply=execute(r);CHECK(reply.applied==S::Ok&&reply.recovery->state==R::Confirmed);
    CHECK(reply.checkpoint.digest==sha256Hex(reply.recovery->priorDigest+"|"+energy_history_detail::batchText(first())));
    CHECK(encodeEnergyHistoryReply(r,reply,&wire));auto old=r;old.recovery=M::Volatile;EnergyHistoryReply parsed;
    CHECK(!decodeEnergyHistoryReply(old,wire,&parsed));auto v1=executeEnergyHistoryRequest(old);CHECK(encodeEnergyHistoryReply(old,v1,&wire)&&!decodeEnergyHistoryReply(r,wire,&parsed));
    for(unsigned kind=0;kind<14;++kind){auto forged=reply;
        if(kind==0)forged.recovery.reset();
        if(kind==1)forged.checkpoint.digest=std::string(64,'0');
        if(kind==2)forged.recovery->priorSequence++;
        if(kind==3)forged.recovery->priorDigest=std::string(64,'0');
        if(kind==4)forged.recovery->batch->observedAtMs++;
        if(kind==5)forged.recovery->batch->readings[0].milliwatts=1;
        if(kind==6)forged.recovery->applied=S::Clock;
        if(kind==7)forged.recovery->state=R::Prepared;
        if(kind==8)forged.recovery->status=S::Storage;
        if(kind==9)forged.recovery->state=R::Empty;
        if(kind==10)forged.recovery->batch.reset();
        if(kind==11)forged.recovery->state=R::Diverged;
        if(kind==12)forged.checkpoint.digest.clear();
        if(kind==13)forged.recovery->state=R::Disabled;
        CHECK(!encodeEnergyHistoryReply(r,forged,&wire));}
    r.recovery=M::Retry;const auto confirmed=bytes(sidecar(file));CHECK(execute(r).applied==S::Replay);CHECK(bytes(sidecar(file))==confirmed);
    r.batch->readings[0].milliwatts=1;CHECK(execute(r).applied==S::Conflict&&bytes(sidecar(file))==confirmed);
    r=req(file,first());CHECK(execute(r).applied==S::SequenceGap&&bytes(sidecar(file))==confirmed);
    // Failures at three distinct durability boundaries survive reconstruction. Inspection is byte-preserving.
    for(const auto &id:std::vector<std::string>{"fixture.exit-prepared","fixture.exit-committed","fixture.bad-ack"}){
        const auto p=root/(id+".db");create(p);auto b=first();b.id=id;r=req(p,b);
        const auto lost=fake.transact(r,cancel,2000);CHECK(lost.writeOutcomeUnknown&&!lost.reply&&lost.transport!=LocalTaskStatus::Ok);
        const auto before=bytes(sidecar(p));const auto inspected=execute(req(p));
        CHECK(inspected.recovery->state==(id=="fixture.bad-ack"?R::Confirmed:R::Prepared));
        CHECK(inspected.checkpoint.sequence==(id=="fixture.exit-prepared"?0u:1u));
        CHECK(inspected.recovery->batch&&energy_history_detail::batchText(*inspected.recovery->batch)==energy_history_detail::batchText(b));
        CHECK(bytes(sidecar(p))==before);
        // SQLite may checkpoint a committed WAL after helper death; logical inspection adds no event or receipt.
        const auto db=bytes(p);CHECK(execute(req(p)).checkpoint.sequence==inspected.checkpoint.sequence);CHECK(bytes(p)==db&&bytes(sidecar(p))==before);
        {EnergyHistoryOwner owner(helper,p,binding(),{},5000,[]{return T+20000;});CHECK(owner.start(1)&&ready(owner));const auto snapshot=owner.snapshot();
            CHECK(snapshot->recoveryState==inspected.recovery->state);
            if(id!="fixture.bad-ack"){
                CHECK(snapshot->state==EnergyHistoryOwnerState::NeedsReview&&snapshot->uncertainBatch);
                CHECK(owner.submit(1,snapshot->revision,second())==EnergyHistoryAdmission::NotReady);
                CHECK(owner.retryReviewed(2,snapshot->revision)==EnergyHistoryAdmission::StaleGeneration);
                CHECK(owner.retryReviewed(1,snapshot->revision-1)==EnergyHistoryAdmission::ReviewChanged);
                EnergyHistoryAdmission a{},bAdmission{};std::thread one([&]{a=owner.retryReviewed(1,snapshot->revision);}),two([&]{bAdmission=owner.retryReviewed(1,snapshot->revision);});one.join();two.join();
                CHECK((a==EnergyHistoryAdmission::Queued)!=(bAdmission==EnergyHistoryAdmission::Queued));CHECK(ready(owner));
                CHECK(!owner.snapshot()->uncertainBatch&&owner.snapshot()->reply->checkpoint.sequence==1);
            }else CHECK(snapshot->state==EnergyHistoryOwnerState::Ready&&!snapshot->uncertainBatch);
            CHECK(owner.submit(1,owner.snapshot()->revision,second())==EnergyHistoryAdmission::Invalid); // old recovered session never becomes the new parent's sampler
            owner.stop();}
        r.recovery=M::Retry;CHECK(client.transact(r,cancel,5000).reply->applied==S::Replay);
    }
    // A failed retry is not a new definite rejection of the old operation.
    const auto failed=root/"retry-failure.db";create(failed);CHECK(execute(req(failed,first())).applied==S::Ok);
    auto bad=second();bad.readings[1].monotonicMs=100;auto invalid=req(failed,bad);prepared(invalid);const auto pending=bytes(sidecar(failed));
    invalid.recovery=M::Retry;reply=execute(invalid);CHECK(reply.applied==S::Clock&&reply.recovery->state==R::Prepared&&!reply.recovery->applied);
    CHECK(bytes(sidecar(failed))==pending);auto changed=invalid;changed.batch=second();CHECK(execute(changed).applied==S::Conflict&&bytes(sidecar(failed))==pending);
    auto blocked=req(failed,second());CHECK(execute(blocked).applied==S::Conflict&&bytes(sidecar(failed))==pending);
    {EnergyHistoryOwner owner(helper,failed,binding(),{},5000,[]{return T+20000;});CHECK(owner.start(1)&&ready(owner));const auto retained=owner.snapshot()->uncertainBatch;
        CHECK(retained&&owner.retryReviewed(1,owner.snapshot()->revision)==EnergyHistoryAdmission::Queued);
        CHECK(until([&]{return owner.snapshot()->state==EnergyHistoryOwnerState::Faulted;}));CHECK(owner.snapshot()->uncertainBatch==retained);
        owner.stop();CHECK(owner.start(2)&&ready(owner));CHECK(owner.snapshot()->state==EnergyHistoryOwnerState::NeedsReview&&bytes(sidecar(failed))==pending);owner.stop();}
    const auto rejected=root/"rejected.db";create(rejected);CHECK(execute(req(rejected,first())).applied==S::Ok);
    reply=execute(req(rejected,bad));CHECK(reply.applied==S::Clock&&reply.recovery->state==R::Rejected);
    invalid=req(rejected,bad);invalid.recovery=M::Retry;const auto rejection=bytes(sidecar(rejected));CHECK(execute(invalid).applied==S::Clock&&bytes(sidecar(rejected))==rejection);
    CHECK(execute(req(rejected,second())).applied==S::Ok);
    // Mismatched journal/ledger prefixes, including an empty journal with unrelated committed facts, never append.
    for(unsigned kind=0;kind<4;++kind){const auto p=root/("diverged-"+std::to_string(kind)+".db");create(p);
        if(kind<3)prepared(req(p,first()),kind==1);
        {EnergyHistoryLedger l;CHECK(l.open(p,binding(),T+20000)==S::Ok);
            if(kind==0){auto different=first();different.readings[0].milliwatts=7;CHECK(l.apply(different,T+20000)==S::Ok);}
            if(kind==1)CHECK(l.apply(second(),T+20000)==S::Ok);
            if(kind==3)CHECK(l.apply(first(),T+20000)==S::Ok);}
        if(kind==2){const auto saved=bytes(sidecar(p));std::filesystem::remove(p);EnergyHistoryLedger l;CHECK(l.open(p,binding(),T+20000,EnergyHistoryOpen::CreateNew)==S::Ok);
            auto different=first();different.id="fixture.other";CHECK(l.apply(different,T+20000)==S::Ok);CHECK(bytes(sidecar(p))==saved);}
        const auto before=bytes(sidecar(p));reply=execute(req(p));CHECK(reply.recovery->state==R::Diverged);
        auto retry=req(p,first());retry.recovery=M::Retry;CHECK(execute(retry).applied==S::Conflict&&bytes(sidecar(p))==before);
        CHECK(execute(req(p,second())).applied==S::Conflict&&bytes(sidecar(p))==before);
    }
    // Missing, truncated, noncanonical and wrongly bound material stays untouched, never reset/recreated.
    for(unsigned kind=0;kind<9;++kind){const auto p=root/("unavailable-"+std::to_string(kind)+".db");create(p);CHECK(execute(req(p,first())).applied==S::Ok);
        auto q=req(p);auto side=sidecar(p);
        if(kind==0)std::filesystem::remove(side);
        if(kind==1)write(side,bytes(side)+" ");
        if(kind==2)write(side,"[");
        if(kind==3)write(side,std::string(energyHistoryRequestLimit+513,' '));
        if(kind==4)q.binding.channels[0]="fixture.wrong";
        if(kind==5)--q.limits.maxWindows;
        if(kind==6)std::filesystem::remove(p);
        if(kind==7)q.nowUtcMs=T+1000;
        if(kind==8){std::filesystem::remove(side);std::filesystem::create_directory(side);}
        const auto before=std::filesystem::is_regular_file(side)?bytes(side):"";const auto db=std::filesystem::exists(p)?bytes(p):"";
        reply=execute(q);CHECK(reply.opened!=S::Ok&&reply.recovery->state==R::Unavailable);
        CHECK((!std::filesystem::is_regular_file(side)||bytes(side)==before)&&(!std::filesystem::exists(p)||bytes(p)==db));
        if(kind==0)CHECK(!std::filesystem::exists(side));
        if(kind==6)CHECK(!std::filesystem::exists(p));}
    const auto orphan=root/"orphan.db";{EnergyHistoryLedger l;CHECK(l.open(orphan,binding(),T+20000,EnergyHistoryOpen::CreateNew)==S::Ok);}
    CHECK(execute(req(orphan)).opened==S::Storage&&!std::filesystem::exists(sidecar(orphan)));
    // Recovery and database leases are both nonblocking and failure preserves the surviving store.
    const auto locked=root/"locked.db";create(locked);const auto lockedBefore=bytes(sidecar(locked));
    {EnergyRecoveryJournal held;CHECK(held.open(req(locked))==S::Ok);EnergyRecoveryJournal contender;CHECK(contender.open(req(locked))==S::Storage);
        const auto result=client.transact(req(locked,first()),cancel,1000);CHECK(result.reply&&result.reply->opened==S::Storage&&result.writeOutcomeUnknown);}
    {EnergyHistoryLedger held;CHECK(held.open(locked,binding(),T+20000)==S::Ok);
        const auto result=client.transact(req(locked,first()),cancel,1000);CHECK(result.reply&&result.reply->opened==S::Storage&&result.writeOutcomeUnknown);
        CHECK(held.verifyCommitted(first(),T+20000)==S::SequenceGap&&held.checkpoint(T+20000).sequence==0);}
    CHECK(bytes(sidecar(locked))==lockedBefore&&execute(req(locked)).checkpoint.sequence==0);
    // Cancellation after durable receipt retains in-memory review and cannot publish late or resume old sampling.
    {const auto p=root/"stopped.db";EnergyHistoryOwner owner(fixture,p,binding(),{},5000,[]{return T+20000;});
        CHECK(owner.start(1,EnergyHistoryOpen::CreateNew)&&ready(owner));auto b=first();b.id="fixture.stall-after";
        CHECK(owner.submit(1,owner.snapshot()->revision,b)==EnergyHistoryAdmission::Queued);auto marker=p;marker+=".fixture-ready";
        CHECK(until([&]{return std::filesystem::exists(marker);}));const auto start=std::chrono::steady_clock::now();owner.requestStop();const auto stopped=owner.snapshot();
        CHECK(std::chrono::steady_clock::now()-start<std::chrono::milliseconds(100));CHECK(stopped->uncertainBatch&&stopped->state==EnergyHistoryOwnerState::Stopped);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));CHECK(owner.snapshot()==stopped);owner.stop();
        CHECK(owner.start(2)&&ready(owner));CHECK(owner.snapshot()->state==EnergyHistoryOwnerState::NeedsReview&&owner.snapshot()->recoveryState==R::Confirmed);
        CHECK(owner.retryReviewed(2,owner.snapshot()->revision)==EnergyHistoryAdmission::Queued&&ready(owner));CHECK(!owner.snapshot()->uncertainBatch);owner.stop();}
    // Largest supported structured original crosses the old JSON-string ceiling through journal, helper and response.
    auto large=req(root/"maximum.db",first());large.binding.channels.clear();large.batch->readings.clear();large.batch->windows.clear();large.mode=EnergyHistoryOpen::CreateNew;
    for(unsigned i=0;i<64;++i){large.binding.channels.push_back("fixture.channel-"+std::string(110,'x')+std::to_string(i));large.batch->readings.push_back(reading(i,0,UINT32_MAX));}
    for(unsigned i=0;i<64;++i)large.batch->readings.push_back(reading(i,1000,0));
    for(unsigned i=0;i<32;++i)large.batch->windows.push_back({"fixture.register-"+std::string(107,'x')+std::to_string(i),std::string(128,'p'),std::string(128,'r'),T,T+4000,false,0});
    auto maximum=client.transact(large,cancel,5000);CHECK(maximum.reply&&maximum.reply->applied==S::Ok&&maximum.reply->recovery->batch->readings.size()==128);
    CHECK(bytes(sidecar(large.file)).size()>20000);large.mode=EnergyHistoryOpen::Existing;large.recovery=M::Retry;
    maximum=client.transact(large,cancel,5000);CHECK(maximum.reply&&maximum.reply->applied==S::Replay&&!maximum.writeOutcomeUnknown);
    large.batch.reset();large.recovery=M::Inspect;maximum=client.transact(large,cancel,5000);CHECK(maximum.reply&&maximum.reply->recovery->batch->windows.size()==32);
    std::filesystem::remove_all(root);return finishTests("energy recovery");
}
