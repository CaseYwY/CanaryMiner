#include "billing_input_fixture.h"
#include "product/billing_compute_owner.h"
#include "product_test_util.h"
#include <chrono>
#include <mutex>
#include <thread>
using namespace billing_input_fixture;
namespace {
using D=BillingComputeAdmission;using O=BillingComputeState;
template<class F>bool until(F f){const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(10);while(std::chrono::steady_clock::now()<end){if(f())return true;std::this_thread::sleep_for(std::chrono::milliseconds(2));}return f();}
template<class T>bool done(T &o){return until([&]{return !o.snapshot()->pending;});}
struct Clock {std::mutex mutex;BillingOwnerTime value{now,10000};BillingOwnerTime get(){std::lock_guard<std::mutex> lock(mutex);return value;}
    void move(int64_t utc,int64_t mono){std::lock_guard<std::mutex> lock(mutex);value.utcMs+=utc;value.monotonicMs=static_cast<uint64_t>(static_cast<int64_t>(value.monotonicMs)+mono);}};
using Context=std::shared_ptr<const BillingOwnerBinding>;
Context context(const std::filesystem::path &root,const std::string &name){const auto dir=root/name;std::filesystem::create_directory(dir);auto c=std::make_shared<BillingOwnerBinding>();c->file=dir/"bill.db";c->binding=binding(dir/"source.db");
    BillingRecoveryStore store;CHECK(store.open(c->file,c->binding,now,BillingRecoveryOpen::CreateNew)==S::Ok);return c;}
std::shared_ptr<BillingOwnerSnapshot> money(const Context &c,uint64_t revision=1){auto s=std::make_shared<BillingOwnerSnapshot>();s->context=c;s->generation=7;s->revision=revision;s->state=BillingOwnerState::Ready;s->reviewable=true;
    auto v=std::make_shared<BillingOwnerView>();v->stable=true;v->observedUtcMs=now;auto stage=std::make_shared<BillingInputSnapshot>();stage->status=S::Ok;v->stage=stage;
    BillingRecoveryStore store;CHECK(store.open(c->file,c->binding,now,BillingRecoveryOpen::QueryOnly)==S::Ok);v->monetary=std::make_shared<const BillingRecoverySnapshot>(store.inspect(now));s->view=v;return s;}
BillingSourceNotice write(const Context &c,unsigned seq=1){const auto reply=executeEnergyHistoryRequest(input(c->binding,seq));CHECK(reply.applied==EnergyHistoryStatus::Ok);return {anchor(reply.checkpoint),reply.checkpoint.throughUtcMs,reply.checkpoint.observedAtMs};}
bool notify(BillingComputeOwner &o,const BillingSourceNotice &n){return o.notify(7,o.snapshot()->revision,n)==D::Queued&&done(o);}
void put(const std::filesystem::path &p,const std::string &bytes){std::ofstream f(p,std::ios::binary|std::ios::trunc);f<<bytes;}
}
int main(int argc,char **argv){
    if(argc!=7)return 2;
    const auto reader=std::filesystem::absolute(argv[1]),fixture=std::filesystem::absolute(argv[2]);
    BillingOwnerHelpers helpers{std::filesystem::absolute(argv[3]),std::filesystem::absolute(argv[4]),std::filesystem::absolute(argv[5]),std::filesystem::absolute(argv[6])};
    const auto root=std::filesystem::path(makeScratchDir("billing-compute"));const auto c=context(root,"normal");const auto notice=write(c);auto review=money(c);Clock clock;
    {BillingComputeOwner disabled(reader,c);CHECK(!disabled.start(7));}
    {BillingComputeOwner stopped(reader,c,{true,10000,512});stopped.requestStop();CHECK(!stopped.start(7));}
    for(unsigned invalid=0;invalid<3;++invalid){auto policy=BillingComputePolicy{true,10000,512};if(invalid==0)policy.maxIntervals=0;if(invalid==1)policy.maxIntervals=513;if(invalid==2)policy.operationTimeoutMs=499;BillingComputeOwner o(reader,c,policy);CHECK(!o.start(7));}
    const auto sourceBefore=bytes(c->binding.source->file),sourceJournal=bytes(journal(c->binding.source->file)),moneyBefore=bytes(c->file),moneyJournal=bytes(side(c->file,".billing-recovery"));
    {BillingComputeOwner o(reader,c,{true,10000,512},[&]{return clock.get();});CHECK(o.start(7)&&o.snapshot()->state==O::Waiting&&o.snapshot()->lastTicket==0);
        CHECK(o.notify(7,o.snapshot()->revision,notice)==D::NotReady&&o.refresh(7,o.snapshot()->revision)==D::NotReady);
        CHECK(!o.observe(8,review)&&o.observe(7,review));CHECK(o.notify(8,o.snapshot()->revision,notice)==D::StaleGeneration&&o.notify(7,0,notice)==D::ReviewChanged);
        auto copied=std::make_shared<BillingOwnerSnapshot>(*review);CHECK(!o.observe(7,copied));
        copied->revision=2;copied->context=std::make_shared<const BillingOwnerBinding>(*c);CHECK(!o.observe(7,copied));
        for(unsigned variant=0;variant<5;++variant){auto bad=notice;if(variant==0)bad.checkpoint.digest=std::string(65,'f');if(variant==1)bad.checkpoint.sequence=0;if(variant==2)bad.throughUtcMs=T;if(variant==3)bad.observedUtcMs=now+1;if(variant==4)bad.throughUtcMs=bad.observedUtcMs+1;
            CHECK(o.notify(7,o.snapshot()->revision,bad)==D::Invalid&&o.snapshot()->lastTicket==0);}
        const auto rev=o.snapshot()->revision;D first=D::Invalid,second=D::Invalid;std::thread a([&]{first=o.notify(7,rev,notice);}),b([&]{second=o.notify(7,rev,notice);});a.join();b.join();CHECK((first==D::Queued)!=(second==D::Queued));CHECK(done(o));
        auto s=o.snapshot();CHECK(s->state==O::Ready&&s->reviewable&&s->candidate&&s->sourceCoverage==EnergyHistoryCoverage::Complete);
        CHECK(s->candidate->reviewed==review&&s->candidate->original.sequence==1&&s->candidate->original.observedUtcMs==T+1000&&s->candidate->queriedUtcMs==now);
        CHECK(s->candidate->original.receipt&&s->candidate->original.receipt->request.source.ticket==1&&s->candidate->preview.provisionalTotal&&s->candidate->preview.provisionalTotal->numerator.text()=="370368");
        CHECK(o.notify(7,s->revision,notice)==D::Duplicate&&o.snapshot()==s);
        CHECK(bytes(c->file)==moneyBefore&&bytes(side(c->file,".billing-recovery"))==moneyJournal&&bytes(c->binding.source->file)==sourceBefore&&bytes(journal(c->binding.source->file))==sourceJournal);
        CHECK(!std::filesystem::exists(side(c->file))); // reads and a preview never create a staged monetary input
        CHECK(o.refresh(7,s->revision)==D::Queued&&done(o)&&o.snapshot()->lastTicket==2);
        auto changed=std::make_shared<BillingOwnerSnapshot>(*review);changed->revision=2;CHECK(o.observe(7,changed)&&!o.snapshot()->reviewable&&o.snapshot()->candidate);
        CHECK(!o.observe(7,review)&&notify(o,notice));CHECK(o.invalidate(7)&&!o.snapshot()->reviewable&&!o.observe(7,changed));
        auto fresh=std::make_shared<BillingOwnerSnapshot>(*changed);fresh->revision=3;CHECK(o.observe(7,fresh)&&notify(o,notice));
        const auto begin=std::chrono::steady_clock::now();o.requestStop();const auto stopped=o.snapshot();o.stop();CHECK(std::chrono::steady_clock::now()-begin<std::chrono::seconds(2)&&o.snapshot()==stopped&&!stopped->reviewable&&stopped->candidate&&!o.start(8));}
    // A candidate is submitted only through the separately reviewed monetary owner and its live CAS.
    {BillingOwner ledger(helpers,*c,{true,10000},[]{return BillingOwnerTime{now,10000};});CHECK(ledger.start(7)&&done(ledger));
        const auto s=ledger.snapshot();BillingComputeOwner o(reader,s->context,{true,10000,512},[]{return BillingOwnerTime{now,10000};});CHECK(o.start(7)&&o.observe(7,s)&&notify(o,notice));
        const auto candidate=o.snapshot()->candidate;CHECK(ledger.refresh(7,s->revision)==BillingOwnerAdmission::Queued&&done(ledger));
        CHECK(ledger.submitReviewed(7,candidate->reviewed->revision,candidate->original)==BillingOwnerAdmission::ReviewChanged);
        CHECK(o.observe(7,ledger.snapshot())&&notify(o,notice));const auto ready=o.snapshot()->candidate;
        CHECK(ledger.submitReviewed(7,ready->reviewed->revision,ready->original)==BillingOwnerAdmission::Queued&&done(ledger));
        CHECK(ledger.snapshot()->actionProof&&ledger.snapshot()->actionProof->committed.provisionalTotal->numerator.compare(ready->preview.provisionalTotal->numerator)==0);
        CHECK(o.observe(7,ledger.snapshot())&&!o.snapshot()->reviewable&&o.notify(7,o.snapshot()->revision,notice)==D::NotReady);
        ledger.requestStop();CHECK(o.observe(7,ledger.snapshot())&&o.snapshot()->state==O::Stopped);}
    // Partial coverage and capacity preserve unavailable rather than a zero/shortened candidate.
    {auto gap=context(root,"gap");auto r=input(gap->binding,1);r.batch->readings.insert(r.batch->readings.begin()+1,{0,T+500,500,0,{}});const auto reply=executeEnergyHistoryRequest(r);CHECK(reply.applied==EnergyHistoryStatus::Ok);
        BillingComputeOwner o(reader,gap,{true,10000,512},[]{return BillingOwnerTime{now,10000};});CHECK(o.start(7)&&o.observe(7,money(gap))&&notify(o,{anchor(reply.checkpoint),T+1000,T+1000}));
        CHECK(o.snapshot()->state==O::Unavailable&&!o.snapshot()->candidate&&o.snapshot()->sourceCoverage==EnergyHistoryCoverage::Partial);}
    {auto cap=context(root,"capacity");write(cap);const auto n=write(cap,2);BillingComputeOwner o(reader,cap,{true,10000,1},[]{return BillingOwnerTime{now,10000};});CHECK(o.start(7)&&o.observe(7,money(cap))&&notify(o,n));
        CHECK(!o.snapshot()->candidate&&o.snapshot()->sourceStatus==EnergyHistoryStatus::Capacity);}
    // Failed source inspection leaves the earlier candidate historical, not silently current.
    {auto missing=context(root,"missing");const auto n=write(missing);BillingComputeOwner o(reader,missing,{true,10000,512},[]{return BillingOwnerTime{now,10000};});CHECK(o.start(7)&&o.observe(7,money(missing))&&notify(o,n));const auto old=o.snapshot()->candidate;
        std::filesystem::remove(missing->binding.source->file);CHECK(o.refresh(7,o.snapshot()->revision)==D::Queued&&done(o));CHECK(!o.snapshot()->reviewable&&o.snapshot()->candidate==old&&o.snapshot()->state==O::Unavailable);}
    // Source rollback and same-sequence branch replacement are detected using actual read proofs, not hints.
    for(bool fork:{false,true}){auto p=context(root,fork?"fork":"rollback");std::string empty,emptyJournal;BillingSourceNotice first;
        if(fork){auto r=input(p->binding,1);r.batch.reset();r.recovery=EnergyRecoveryMode::Inspect;CHECK(executeEnergyHistoryRequest(r).opened==EnergyHistoryStatus::Ok);
            empty=bytes(p->binding.source->file);emptyJournal=bytes(journal(p->binding.source->file));r=input(p->binding,1);r.mode=EnergyHistoryOpen::Existing;const auto reply=executeEnergyHistoryRequest(r);CHECK(reply.applied==EnergyHistoryStatus::Ok);
            first={anchor(reply.checkpoint),reply.checkpoint.throughUtcMs,reply.checkpoint.observedAtMs};}
        else first=write(p);
        const auto db=bytes(p->binding.source->file),saved=bytes(journal(p->binding.source->file));
        BillingSourceNotice latest=first;if(!fork)latest=write(p,2);
        BillingComputeOwner o(reader,p,{true,10000,512},[]{return BillingOwnerTime{now,10000};});CHECK(o.start(7)&&o.observe(7,money(p))&&notify(o,latest));
        if(fork){put(p->binding.source->file,empty);put(journal(p->binding.source->file),emptyJournal);auto r=input(p->binding,1,77777);r.mode=EnergyHistoryOpen::Existing;CHECK(executeEnergyHistoryRequest(r).applied==EnergyHistoryStatus::Ok);}
        else{put(p->binding.source->file,db);put(journal(p->binding.source->file),saved);}
        CHECK(o.refresh(7,o.snapshot()->revision)==D::Queued&&done(o));CHECK(o.snapshot()->state==O::Faulted&&o.snapshot()->status==S::Attribution&&!o.snapshot()->reviewable);}
    // Clock faults, invalidation and stop revoke even a valid reply held by the synthetic helper.
    for(unsigned action=0;action<4;++action){auto p=context(root,"held-"+std::to_string(action));const auto n=write(p);Clock local;
        BillingComputeOwner o(fixture,p,{true,action==3?500u:10000u,512},[&]{return local.get();});const auto original=money(p);CHECK(o.start(7)&&o.observe(7,original));
        CHECK(o.notify(7,o.snapshot()->revision,n)==D::Queued&&until([&]{return std::filesystem::exists(side(p->binding.source->file,".fixture-ready"));}));
        CHECK(o.notify(7,o.snapshot()->revision,n)==D::NotReady);
        if(action==0){const auto begin=std::chrono::steady_clock::now();o.requestStop();CHECK(std::chrono::steady_clock::now()-begin<std::chrono::milliseconds(100));}
        if(action==1)CHECK(o.invalidate(7));
        if(action==2)local.move(-1,0);
        if(action!=3)put(side(p->binding.source->file,".fixture-release"),"1");
        CHECK(done(o)&&!o.snapshot()->candidate&&!o.snapshot()->reviewable);
        if(action==0)CHECK(o.snapshot()->state==O::Stopped);
        if(action==1)CHECK(o.snapshot()->state==O::Waiting&&!o.observe(7,original));
        if(action==2)CHECK(o.snapshot()->state==O::ClockFault);
        if(action==3){CHECK(o.snapshot()->transport==LocalTaskStatus::Timeout&&o.snapshot()->status==S::Storage);const auto old=o.snapshot();std::this_thread::sleep_for(std::chrono::milliseconds(75));CHECK(o.snapshot()==old);}}
    for(unsigned fault=0;fault<3;++fault){auto p=context(root,"clock-"+std::to_string(fault));Clock local;BillingComputeOwner o(reader,p,{true,10000,512},[&]{return local.get();});CHECK(o.start(7));
        local.move(fault==0?-1:fault==1?3001:0,fault==2?-1:0);CHECK(until([&]{return o.snapshot()->state==O::ClockFault;})&&!o.start(8));}
    // A notification's invented head cannot become a high-water mark or overwrite the proven receipt.
    {auto p=context(root,"hint");auto n=write(p);n.checkpoint={1000,std::string(64,'f')};
        BillingComputeOwner o(reader,p,{true,10000,512},[]{return BillingOwnerTime{now,10000};});CHECK(o.start(7)&&o.observe(7,money(p))&&notify(o,n));
        CHECK(o.snapshot()->candidate->original.receipt->reply.source.checkpoint.sequence==1);
        const auto next=write(p,2);CHECK(notify(o,next)&&o.snapshot()->reviewable&&o.snapshot()->candidate->original.receipt->reply.source.checkpoint.sequence==2);}
    // Reconstructed monetary rollback/fork is terminal even after explicit invalidation.
    for(bool fork:{false,true}){auto p=context(root,fork?"money-fork":"money-rollback");write(p);
        const auto db=bytes(p->file),saved=bytes(side(p->file,".billing-recovery"));
        const auto q=query(p->binding,{0,sha256Hex(energy_history_detail::bindingText(p->binding.source->binding))});BillingChunk original;
        CHECK(makeBillingSourceChunk(1,q,executeEnergyTraceRequest(q),&original)==S::Ok);
        {BillingRecoveryStore store;CHECK(store.open(p->file,p->binding,now)==S::Ok&&store.submit(original,now)==S::Ok);}
        BillingComputeOwner o(reader,p,{true,10000,512},[]{return BillingOwnerTime{now,10000};});CHECK(o.start(7)&&o.observe(7,money(p)));
        CHECK(o.invalidate(7));put(p->file,db);put(side(p->file,".billing-recovery"),saved);
        if(fork){original.id="fixture.alternative-original";BillingRecoveryStore store;CHECK(store.open(p->file,p->binding,now)==S::Ok&&store.submit(original,now)==S::Ok);}
        CHECK(o.observe(7,money(p,2))&&o.snapshot()->state==O::Faulted&&!o.snapshot()->reviewable&&o.refresh(7,o.snapshot()->revision)==D::Stopped);}
    std::filesystem::remove_all(root);return finishTests("billing compute");
}
