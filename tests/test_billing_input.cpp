#include "billing_input_fixture.h"
#include "product/local_store_lease.h"
#include "product_test_util.h"
#include <chrono>
#include <thread>
using namespace billing_input_fixture;
namespace {
template<class F>bool until(F f){const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);while(std::chrono::steady_clock::now()<end){if(f())return true;std::this_thread::sleep_for(std::chrono::milliseconds(2));}return f();}
BillingInputRequest upload(BillingInputProcessClient &client,const BillingInputPlan &p,const std::atomic<bool> &cancel,const std::string &priorStage={}){
    std::string payload;auto r=request(p,&payload);r.expectedRevision=priorStage;auto result=client.exchange(r,cancel,10000);CHECK(result.reply&&result.reply->status==S::Ok);
    if(!result.reply)return r;
    for(size_t offset=0;offset<payload.size();offset+=billingInputPageBytes){r.action=A::Append;r.offset=offset;r.data=payload.substr(offset,billingInputPageBytes);r.expectedRevision=result.reply->revision;
        result=client.exchange(r,cancel,10000);CHECK(result.reply&&result.reply->status==S::Ok);if(!result.reply)return r;}
    r.action=A::Commit;r.offset=0;r.data.clear();r.expectedRevision=result.reply->revision;return r;
}
}
int main(int argc,char **argv){
    if(argc!=4)return 2;
    const auto helper=std::filesystem::absolute(argv[1]),fixture=std::filesystem::absolute(argv[2]);BillingInputProcessClient client(helper);std::atomic<bool> cancel{false};
    const auto root=std::filesystem::path(makeScratchDir("billing-input"));auto p=plan(root/"bill.db");
    auto created=client.transact(p,"",cancel,10000);CHECK(created.transport==LocalTaskStatus::Ok&&created.confirmed&&created.confirmed->state==R::Empty&&!created.confirmed->committed.provisionalTotal);
    if(!created.reply||!created.confirmed)return 3;
    auto completed=created.reply->revision;const auto empty=bytes(side(p.context.file,".billing-recovery"));
    CHECK(client.transact(p,"",cancel,10000).reply->status==S::Conflict&&bytes(side(p.context.file,".billing-recovery"))==empty);
    p.chunk=billing_fixture::chunk();p.prior=created.confirmed;
    const auto committed=client.transact(p,completed,cancel,10000);CHECK(committed.confirmed&&committed.confirmed->state==R::Confirmed&&committed.confirmed->committed.sequence==1&&committed.commitIssued);
    if(!committed.confirmed||!committed.reply)return 4;
    completed=committed.reply->revision;
    {std::string payload;auto r=request(p,&payload);r.action=A::Inspect;const auto before=bytes(side(p.context.file));const auto read=client.exchange(r,cancel,5000);
        CHECK(read.reply&&read.reply->phase==P::Complete&&read.reply->data==payload&&bytes(side(p.context.file))==before);
        r.expectedRevision=read.reply->revision;r.action=A::Commit;CHECK(client.exchange(r,cancel,5000).reply->status==S::Conflict&&bytes(side(p.context.file))==before);}
    p.prior=committed.confirmed;p.chunk=billing_fixture::chunk(2);p.chunk->trace.intervals[0].beginUtcMs++;
    const auto rejected=client.transact(p,completed,cancel,10000);CHECK(rejected.confirmed&&rejected.confirmed->state==R::Rejected&&rejected.confirmed->applied==S::Calculation&&rejected.confirmed->committed.sequence==1);
    if(!rejected.confirmed||!rejected.reply)return 5;
    p.prior=rejected.confirmed;p.chunk=billing_fixture::chunk(2);
    const auto corrected=client.transact(p,rejected.reply->revision,cancel,10000);CHECK(corrected.confirmed&&corrected.confirmed->committed.sequence==2);
    // Full near-1-MiB escaped binding plus 512 exact intervals, across fixed request/response budgets.
    {auto big=plan(root/std::filesystem::u8path("fixture-\xe6\xb5\x8b\xe8\xaf\x95.db"));big.context.binding=largeBinding();
        CHECK(billing_detail::bindingText(big.context.binding,{}).size()>billing_detail::bindingLimit-10*1024);
        const auto opened=client.transact(big,"",cancel,30000);CHECK(opened.confirmed&&opened.confirmed->state==R::Empty);
        if(!opened.confirmed)return 6;
        big.prior=opened.confirmed;big.chunk=largeChunk();const auto applied=client.transact(big,opened.reply->revision,cancel,30000);
        CHECK(applied.confirmed&&applied.confirmed->committed.sequence==1&&applied.confirmed->original->trace.intervals.size()==512&&bytes(side(big.context.file)).size()>1024*1024);
        std::string payload;auto r=request(big,&payload);r.action=A::Inspect;r.expectedRevision=applied.reply->revision;std::string assembled;
        for(size_t offset=0;offset<payload.size();offset+=billingInputPageBytes){r.offset=offset;const auto read=client.exchange(r,cancel,10000);CHECK(read.reply&&read.reply->revision==applied.reply->revision);if(!read.reply)return 7;assembled+=read.reply->data;}
        CHECK(assembled==payload);
        for(auto &revision:big.context.binding.plan.revisions)for(auto &c:revision.calendar)c.key+=std::string(10,'"');
        CHECK(client.transact(big,applied.reply->revision,cancel,10000).transport==LocalTaskStatus::Invalid);}
    // New input preserves a frozen complete source receipt, without touching the energy source during commit.
    {auto bound=plan(root/"source-bill.db");bound.context.binding=binding(root/"source.db");bound.context.nowUtcMs=now;
        const auto opened=client.transact(bound,"",cancel,10000);CHECK(opened.confirmed.has_value());bound.prior=opened.confirmed;
        CHECK(executeEnergyHistoryRequest(input(bound.context.binding,1)).applied==EnergyHistoryStatus::Ok);
        const auto q=query(bound.context.binding,{0,sha256Hex(energy_history_detail::bindingText(bound.context.binding.source->binding))});BillingChunk chunk;
        CHECK(makeBillingSourceChunk(1,q,executeEnergyTraceRequest(q),&chunk)==S::Ok);bound.chunk=chunk;
        std::filesystem::remove(bound.context.binding.source->file);std::filesystem::remove(journal(bound.context.binding.source->file));
        const auto applied=client.transact(bound,opened.reply->revision,cancel,10000);
        CHECK(applied.confirmed&&applied.confirmed->committed.source->checkpoint.sequence==1&&!std::filesystem::exists(bound.context.binding.source->file));}
    // Sequential staging CAS: no skipped/repeated/mixed pages and no authority before the complete payload.
    {auto q=plan(root/"pages.db");q.context.binding=largeBinding();std::string payload;auto r=request(q,&payload);const auto opened=client.exchange(r,cancel,5000);CHECK(opened.reply&&opened.reply->received==0);
        r.expectedRevision=opened.reply->revision;r.action=A::Commit;CHECK(client.exchange(r,cancel,5000).reply->status==S::Conflict&&!std::filesystem::exists(q.context.file));
        r.action=A::Append;r.data=payload.substr(0,billingInputPageBytes);const auto first=client.exchange(r,cancel,5000);CHECK(first.reply&&first.reply->received==billingInputPageBytes);
        const auto frozen=bytes(side(q.context.file));CHECK(client.exchange(r,cancel,5000).reply->status==S::Conflict&&bytes(side(q.context.file))==frozen);
        auto bad=r;bad.expectedRevision=first.reply->revision;bad.offset=2*billingInputPageBytes;CHECK(client.exchange(bad,cancel,5000).reply->status==S::Conflict);
        bad.offset=billingInputPageBytes;bad.manifest.bindingDigest=bad.manifest.priorDigest=std::string(64,'f');
        const auto mixed=client.exchange(bad,cancel,5000);CHECK(mixed.reply&&mixed.reply->status==S::Conflict&&bytes(side(q.context.file))==frozen);
        r.action=A::Inspect;r.offset=0;r.data.clear();r.expectedRevision=first.reply->revision;CHECK(client.exchange(r,cancel,5000).reply->data==payload.substr(0,billingInputPageBytes));
        r.action=A::Begin;CHECK(client.exchange(r,cancel,5000).reply->status==S::Conflict&&bytes(side(q.context.file))==frozen);
        {LocalStoreLease lock;CHECK(lock.acquire(side(q.context.file,".billing-input.lock")));r.action=A::Inspect;CHECK(client.exchange(r,cancel,5000).reply->status==S::Storage);}}
    {auto q=plan(root/"concurrent.db");std::string payload;auto r=request(q,&payload);const auto opened=client.exchange(r,cancel,5000);
        r.expectedRevision=opened.reply->revision;r.action=A::Append;r.data=payload;BillingInputProcessClient other(helper);BillingInputResult a,b;
        std::thread one([&]{a=client.exchange(r,cancel,10000);}),two([&]{b=other.exchange(r,cancel,10000);});one.join();two.join();
        CHECK(a.reply&&b.reply&&((a.reply->status==S::Ok)!=(b.reply->status==S::Ok)));
        const auto &loser=a.reply->status==S::Ok?b:a;CHECK(loser.reply->status==S::Conflict||loser.reply->status==S::Storage);
        r.action=A::Inspect;r.data.clear();r.expectedRevision.clear();const auto current=client.exchange(r,cancel,5000);
        CHECK(current.reply&&current.reply->received==payload.size()&&current.reply->phase==P::Receiving&&!std::filesystem::exists(q.context.file));}
    // Journal revision and monetary head must still match at actual submit, after all stage pages.
    {auto q=plan(root/"stale.db");{BillingRecoveryStore store;CHECK(store.open(q.context.file,q.context.binding,at,BillingRecoveryOpen::CreateNew)==S::Ok);}q.prior=inspect(q);q.chunk=billing_fixture::chunk();
        auto r=upload(client,q,cancel);const auto prepared=bytes(side(q.context.file));
        {BillingRecoveryStore store;CHECK(store.open(q.context.file,q.context.binding,at)==S::Ok&&store.submit(*q.chunk,at)==S::Ok);}
        const auto original=bytes(side(q.context.file,".billing-recovery"));CHECK(client.exchange(r,cancel,5000).reply->status==S::Conflict&&bytes(side(q.context.file,".billing-recovery"))==original&&bytes(side(q.context.file))!=prepared);
        r.action=A::Inspect;r.expectedRevision.clear();const auto issued=client.exchange(r,cancel,5000);CHECK(issued.reply&&issued.reply->phase==P::Issued);
        // Identical already-committed original can be proven, but never applied again by Reconcile.
        r.action=A::Reconcile;r.expectedRevision=issued.reply->revision;CHECK(client.exchange(r,cancel,5000).reply->phase==P::Complete&&bytes(side(q.context.file,".billing-recovery"))==original);}
    {auto q=plan(root/"locked.db");{BillingRecoveryStore store;CHECK(store.open(q.context.file,q.context.binding,at,BillingRecoveryOpen::CreateNew)==S::Ok);}q.prior=inspect(q);q.chunk=billing_fixture::chunk();
        auto r=upload(client,q,cancel);{LocalStoreLease lock;CHECK(lock.acquire(side(q.context.file,".billing-recovery.lock")));CHECK(client.exchange(r,cancel,5000).reply->status==S::Storage);}
        r.action=A::Inspect;r.expectedRevision.clear();const auto issued=client.exchange(r,cancel,5000);r.action=A::Reconcile;r.expectedRevision=issued.reply->revision;const auto unchanged=client.exchange(r,cancel,5000);
        CHECK(unchanged.reply&&unchanged.reply->phase==P::Complete&&unchanged.reply->certificate->state==R::Unavailable&&unchanged.reply->certificate->result==S::Conflict&&inspect(q).committed.sequence==0);}
    {auto q=plan(root/"head-diverged.db");{BillingRecoveryStore store;CHECK(store.open(q.context.file,q.context.binding,at,BillingRecoveryOpen::CreateNew)==S::Ok);}q.prior=inspect(q);q.chunk=billing_fixture::chunk();
        auto r=upload(client,q,cancel);const auto original=bytes(side(q.context.file,".billing-recovery"));
        {BillingLedger bypass;CHECK(bypass.open(q.context.file,q.context.binding,at)==S::Ok&&bypass.apply(*q.chunk,at)==S::Ok);}
        const auto refused=client.exchange(r,cancel,5000);CHECK(refused.reply&&refused.reply->status==S::Conflict&&bytes(side(q.context.file,".billing-recovery"))==original);
        r.action=A::Inspect;r.expectedRevision.clear();const auto issued=client.exchange(r,cancel,5000);r.action=A::Reconcile;r.expectedRevision=issued.reply->revision;
        CHECK(client.exchange(r,cancel,5000).reply->status==S::Conflict&&bytes(side(q.context.file,".billing-recovery"))==original);}
    {auto q=plan(root/"batch-cap.db");q.context.limits.maxBatches=1;const auto opened=client.transact(q,"",cancel,10000);q.prior=opened.confirmed;q.chunk=billing_fixture::chunk();
        const auto first=client.transact(q,opened.reply->revision,cancel,10000);q.prior=first.confirmed;q.chunk=billing_fixture::chunk(2);
        const auto full=client.transact(q,first.reply->revision,cancel,10000);CHECK(full.confirmed&&full.confirmed->state==R::Rejected&&full.confirmed->applied==S::Capacity&&full.confirmed->committed.sequence==1);}
    {auto q=plan(root/"page-cap.db");q.context.limits.maxDatabaseBytes=1024*1024;const auto opened=client.transact(q,"",cancel,10000);q.prior=opened.confirmed;std::string prior=opened.reply->revision;bool full=false;
        for(unsigned seq=1;seq<=128&&!full;++seq){auto c=billing_fixture::chunk();c.sequence=seq;c.id="fixture.capacity."+std::to_string(seq);c.trace.intervals.clear();
            for(unsigned i=0;i<512;++i){const auto begin=T+static_cast<int64_t>((seq-1)*512+i);c.trace.intervals.push_back({begin,begin+1,d("12345.6789012345678901234567890123456789")});}
            c.observedUtcMs=c.trace.intervals.back().endUtcMs;q.chunk=std::move(c);const auto result=client.transact(q,prior,cancel,10000);
            if(result.reply&&result.reply->status==S::Capacity){full=true;const auto current=inspect(q);CHECK(result.commitIssued&&!result.confirmed&&current.state==R::Prepared&&current.committed.sequence==seq-1);
                const auto stage=bytes(side(q.context.file)),journal=bytes(side(q.context.file,".billing-recovery"));std::string payload;auto r=request(q,&payload);r.action=A::Inspect;
                const auto read=client.exchange(r,cancel,5000);CHECK(read.reply&&read.reply->phase==P::Issued);r.action=A::Reconcile;r.expectedRevision=read.reply->revision;
                CHECK(client.exchange(r,cancel,5000).reply->status==S::Conflict&&bytes(side(q.context.file))==stage&&bytes(side(q.context.file,".billing-recovery"))==journal);
                const auto denied=client.transact(q,prior,cancel,5000);CHECK(denied.reply&&denied.reply->status==S::Conflict&&bytes(side(q.context.file))==stage);
            }else{CHECK(result.confirmed&&result.confirmed->state==R::Confirmed);if(!result.confirmed)return 8;q.prior=result.confirmed;prior=result.reply->revision;}}
        CHECK(full);}
    // Canonical codecs and operation identity bind every acknowledgement, including otherwise identical page bytes.
    {auto q=plan(root/"codec.db");std::string payload,wire;auto r=request(q,&payload);BillingInputRequest decoded;uint64_t parent=0;
        CHECK(encodeBillingInputRequest(r,localTaskProcessId(),&wire)&&decodeBillingInputRequest(wire,&decoded,&parent));CHECK(!decodeBillingInputRequest(wire+" ",&decoded,&parent));
        const auto result=client.exchange(r,cancel,5000);BillingInputReply reply;CHECK(encodeBillingInputReply(r,*result.reply,&wire)&&decodeBillingInputReply(r,wire,&reply));
        for(unsigned k=0;k<5;++k){auto bad=r;if(k==0)bad.generation++;if(k==1)bad.ticket++;if(k==2)bad.action=A::Inspect;if(k==3)bad.manifest.preparedUtcMs++;if(k==4)bad.expectedRevision=std::string(64,'f');CHECK(!decodeBillingInputReply(bad,wire,&reply));}
        CHECK(!decodeBillingInputReply(r,wire+"x",&reply));
        for(unsigned k=0;k<9;++k){auto bad=r;if(k==0)bad.generation=0;if(k==1)bad.ticket=0;if(k==2)bad.manifest.bindingBytes=billing_detail::bindingLimit+1;
            if(k==3)bad.manifest.chunkBytes=billing_detail::chunkLimit+1;
            if(k==4)bad.manifest.file="relative";
            if(k==5)bad.expectedRevision="x";
            if(k==6)bad.manifest.preparedUtcMs=0;
            if(k==7){bad.action=A::Append;bad.expectedRevision=result.reply->revision;bad.data=payload+" ";}
            if(k==8)bad.action=static_cast<A>(9);
            CHECK(!encodeBillingInputRequest(bad,localTaskProcessId(),&wire));}
        {LocalTaskProcess wrong(std::filesystem::absolute(argv[3]));CHECK(encodeBillingInputRequest(r,localTaskProcessId(),&wire)&&wrong.run(wire,cancel,5000).status==LocalTaskStatus::Unavailable);}
#if defined(__linux__)
        {LocalTaskProcess wrong(helper);CHECK(encodeBillingInputRequest(r,localTaskProcessId()==1?2:1,&wire)&&wrong.run(wire,cancel,5000).status==LocalTaskStatus::Unavailable);}
#endif
    }
    for(const auto *name:{"fixture.billing-input-lost.db","fixture.billing-input-proof.db"}){auto q=plan(root/name);BillingInputProcessClient fake(fixture);const auto result=fake.transact(q,"",cancel,5000);
        CHECK(result.transport==LocalTaskStatus::Unavailable&&!result.confirmed&&result.commitIssued&&inspect(q).state==R::Empty);}
    {auto q=plan(root/"fixture.billing-input-hang.db");BillingInputProcessClient stalled(fixture);BillingInputResult result;
        std::thread running([&]{result=stalled.transact(q,"",cancel,5000);});CHECK(until([&]{return std::filesystem::exists(side(q.context.file,".fixture-ready"));}));
        CHECK(stalled.transact(q,"",cancel,5000).transport==LocalTaskStatus::Busy);cancel=true;running.join();CHECK(result.transport==LocalTaskStatus::Cancelled&&!result.confirmed);cancel=false;
        CHECK(stalled.transact(q,"",cancel,100).transport==LocalTaskStatus::Timeout);cancel=true;CHECK(client.transact(q,"",cancel,5000).transport==LocalTaskStatus::Cancelled);cancel=false;
        CHECK(!std::filesystem::exists(q.context.file)&&!std::filesystem::exists(side(q.context.file)));}
    std::filesystem::remove_all(root);return finishTests("billing input");
}
