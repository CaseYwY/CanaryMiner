#include "billing_input_fixture.h"
#include "product/billing_owner.h"
#include "product_test_util.h"
#include <chrono>
#include <mutex>
#include <thread>
using namespace billing_input_fixture;
namespace {
using D=BillingOwnerAdmission;using O=BillingOwnerState;
template<class F>bool until(F f){const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(15);while(std::chrono::steady_clock::now()<end){if(f())return true;std::this_thread::sleep_for(std::chrono::milliseconds(2));}return f();}
struct Clock {
    std::mutex mutex;BillingOwnerTime value{at,10000};
    BillingOwnerTime get(){std::lock_guard<std::mutex> lock(mutex);return value;}
    void advance(int64_t utc,int64_t mono){std::lock_guard<std::mutex> lock(mutex);value.utcMs+=utc;value.monotonicMs=static_cast<uint64_t>(static_cast<int64_t>(value.monotonicMs)+mono);}
};
BillingOwnerPolicy policy(unsigned ms=10000){return {true,ms};}
BillingOwnerBinding context(const BillingInputPlan &p){return {p.context.file,p.context.binding,p.context.limits};}
bool completed(BillingOwner &o){return until([&]{return !o.snapshot()->pending;});}
bool refresh(BillingOwner &o){const auto s=o.snapshot();return o.refresh(s->generation,s->revision)==D::Queued&&completed(o);}
BillingInputReply stage(BillingInputProcessClient &writer,const BillingInputPlan &p,const std::string &previous={},bool partial=false){
    std::atomic<bool> cancel{false};std::string payload;auto r=request(p,&payload);r.expectedRevision=previous;auto result=writer.exchange(r,cancel,10000);
    CHECK(result.reply&&result.reply->status==S::Ok);if(!result.reply)return {};
    for(size_t offset=0;offset<payload.size();offset+=billingInputPageBytes){r.action=A::Append;r.offset=offset;r.data=payload.substr(offset,billingInputPageBytes);r.expectedRevision=result.reply->revision;
        result=writer.exchange(r,cancel,10000);CHECK(result.reply&&result.reply->status==S::Ok);if(!result.reply)return {};if(partial)break;}
    return *result.reply;
}
void put(const std::filesystem::path &p,const std::string &s){std::ofstream f(p,std::ios::binary|std::ios::trunc);f<<s;}
}
int main(int argc,char **argv){
    if(argc!=7)return 2;
    BillingOwnerHelpers helpers{std::filesystem::absolute(argv[1]),std::filesystem::absolute(argv[2]),std::filesystem::absolute(argv[3]),std::filesystem::absolute(argv[4])};
    const auto fixture=std::filesystem::absolute(argv[5]),inputFixture=std::filesystem::absolute(argv[6]);
    const auto root=std::filesystem::path(makeScratchDir("billing-owner"));BillingInputProcessClient writer(helpers.input);std::atomic<bool> cancel{false};
    {auto p=plan(root/"disabled.db");BillingOwner disabled(helpers,context(p));CHECK(!disabled.start(1)&&!std::filesystem::exists(side(p.context.file,".billing-input.lock")));}
    {auto p=plan(root/"stopped.db");BillingOwner stopped(helpers,context(p),policy());stopped.requestStop();CHECK(!stopped.start(1)&&!std::filesystem::exists(p.context.file));}
    {auto p=plan(root/"invalid.db");BillingOwner owner(helpers,context(p),policy(499));CHECK(!owner.start(1));}
    {auto p=plan(root/"normal.db");Clock clock;BillingOwner owner(helpers,context(p),policy(),[&]{return clock.get();});
        CHECK(owner.start(11)&&completed(owner));auto s=owner.snapshot();CHECK(s->reviewable&&s->view->stable&&!s->view->stage->present&&s->view->monetary->status==S::Storage&&!std::filesystem::exists(p.context.file));
        CHECK(owner.createReviewed(12,s->revision)==D::StaleGeneration&&owner.createReviewed(11,s->revision-1)==D::ReviewChanged);
        D first=D::Invalid,second=D::Invalid;std::thread a([&]{first=owner.createReviewed(11,s->revision);}),b([&]{second=owner.createReviewed(11,s->revision);});a.join();b.join();
        CHECK((first==D::Queued)!=(second==D::Queued));CHECK(completed(owner));s=owner.snapshot();
        CHECK(!s->reviewable&&!s->uncertain&&s->actionProof&&s->actionProof->state==R::Empty&&!s->actionProof->committed.provisionalTotal);
        CHECK(owner.submitReviewed(11,s->revision,billing_fixture::chunk())==D::NotReady);CHECK(refresh(owner));s=owner.snapshot();CHECK(s->state==O::Ready);
        const auto reviewed=s->revision;CHECK(owner.submitReviewed(11,reviewed,billing_fixture::chunk())==D::Queued&&completed(owner));s=owner.snapshot();
        CHECK(s->actionProof&&s->actionProof->committed.sequence==1&&!s->uncertain&&!s->reviewable&&s->view->monetary->committed.sequence==0);
        CHECK(owner.refresh(11,reviewed)==D::ReviewChanged&&refresh(owner));s=owner.snapshot();CHECK(s->state==O::Ready&&s->view->monetary->committed.sequence==1);
        auto bad=billing_fixture::chunk(2);bad.trace.intervals[0].beginUtcMs++;
        CHECK(owner.submitReviewed(11,s->revision,bad)==D::Queued&&completed(owner));s=owner.snapshot();
        CHECK(s->actionProof&&s->actionProof->state==R::Rejected&&s->status==S::Calculation&&!s->uncertain&&refresh(owner));s=owner.snapshot();
        CHECK(owner.submitReviewed(11,s->revision,billing_fixture::chunk(2))==D::Queued&&completed(owner)&&refresh(owner));s=owner.snapshot();CHECK(s->view->monetary->committed.sequence==2);
        const auto start=std::chrono::steady_clock::now();owner.requestStop();CHECK(std::chrono::steady_clock::now()-start<std::chrono::milliseconds(100));
        const auto stopped=owner.snapshot();owner.stop();CHECK(owner.snapshot()==stopped&&!stopped->pending&&!stopped->reviewable&&stopped->view->monetary->committed.sequence==2&&!owner.start(12));
        CHECK(owner.refresh(11,stopped->revision)==D::Stopped);}
    // Complete Receiving inputs require explicit Commit; partial originals and temporary ambiguity remain preserved.
    {auto p=plan(root/"ready.db");const auto staged=stage(writer,p);Clock clock;BillingOwner owner(helpers,context(p),policy(),[&]{return clock.get();});
        CHECK(owner.start(1)&&completed(owner));auto s=owner.snapshot();CHECK(s->view->stage->phase==P::Receiving&&s->view->stage->revision==staged.revision&&!std::filesystem::exists(p.context.file));
        CHECK(owner.createReviewed(1,s->revision)==D::NotReady&&owner.commitReviewed(1,s->revision)==D::Queued&&completed(owner));CHECK(owner.snapshot()->actionProof&&refresh(owner)&&owner.snapshot()->state==O::Ready);}
    {auto p=plan(root/"partial.db");p.context.binding=largeBinding();stage(writer,p,"",true);const auto before=bytes(side(p.context.file));Clock clock;
        BillingOwner owner(helpers,context(p),policy(30000),[&]{return clock.get();});CHECK(owner.start(2)&&completed(owner));const auto s=owner.snapshot();
        CHECK(s->view->stage->received==billingInputPageBytes&&!s->view->stage->original&&owner.commitReviewed(2,s->revision)==D::NotReady&&owner.createReviewed(2,s->revision)==D::NotReady&&bytes(side(p.context.file))==before);}
    {auto p=plan(root/"temporary.db");put(side(p.context.file,".billing-input.tmp"),"fixture unknown bytes");Clock clock;BillingOwner owner(helpers,context(p),policy(),[&]{return clock.get();});
        CHECK(owner.start(3)&&completed(owner));const auto s=owner.snapshot();CHECK(s->view->stage->replacementPending&&!s->view->stage->present&&owner.createReviewed(3,s->revision)==D::NotReady);}
    // A competing monetary update after review is caught before Begin, with no new stage at all.
    {auto p=plan(root/"changed.db");{BillingRecoveryStore store;CHECK(store.open(p.context.file,p.context.binding,at,BillingRecoveryOpen::CreateNew)==S::Ok);}
        Clock clock;BillingOwner owner(helpers,context(p),policy(),[&]{return clock.get();});CHECK(owner.start(4)&&completed(owner));const auto s=owner.snapshot();
        {BillingRecoveryStore store;CHECK(store.open(p.context.file,p.context.binding,at)==S::Ok&&store.submit(billing_fixture::chunk(),at)==S::Ok);}
        CHECK(owner.submitReviewed(4,s->revision,billing_fixture::chunk())==D::Queued&&completed(owner));
        CHECK(owner.snapshot()->status==S::Conflict&&!owner.snapshot()->uncertain&&!std::filesystem::exists(side(p.context.file))&&owner.snapshot()->view->monetary->committed.sequence==1);}
    // The stage bracket detects an input arriving during the monetary query and discards the mixed view.
    {auto p=plan(root/"fixture.billing-owner-between.db");const auto created=writer.transact(p,"",cancel,10000);CHECK(created.confirmed.has_value());auto h=helpers;h.monetaryQuery=fixture;Clock clock;
        BillingOwner owner(h,context(p),policy(),[&]{return clock.get();});CHECK(owner.start(5));CHECK(until([&]{return std::filesystem::exists(side(p.context.file,".fixture-ready"));}));
        p.prior=created.confirmed;p.chunk=billing_fixture::chunk();stage(writer,p,created.reply->revision);put(side(p.context.file,".fixture-release"),"1");CHECK(completed(owner));
        CHECK(owner.snapshot()->status==S::Conflict&&!owner.snapshot()->reviewable&&!owner.snapshot()->view&&refresh(owner));CHECK(owner.snapshot()->view->stage->phase==P::Receiving);}
    // Live ordered journal/head checks still refuse a change after the preflight read.
    {auto p=plan(root/"fixture.billing-owner-race.db");{BillingRecoveryStore store;CHECK(store.open(p.context.file,p.context.binding,at,BillingRecoveryOpen::CreateNew)==S::Ok);}
        auto h=helpers;h.input=fixture;Clock clock;BillingOwner owner(h,context(p),policy(),[&]{return clock.get();});CHECK(owner.start(6)&&completed(owner));auto s=owner.snapshot();
        CHECK(owner.submitReviewed(6,s->revision,billing_fixture::chunk())==D::Queued);CHECK(until([&]{return std::filesystem::exists(side(p.context.file,".fixture-ready"));}));
        {BillingRecoveryStore store;auto different=billing_fixture::chunk();different.id="fixture.other";CHECK(store.open(p.context.file,p.context.binding,at)==S::Ok&&store.submit(different,at)==S::Ok);}
        const auto journal=bytes(side(p.context.file,".billing-recovery"));put(side(p.context.file,".fixture-release"),"1");CHECK(completed(owner)&&owner.snapshot()->uncertain&&refresh(owner));s=owner.snapshot();
        CHECK(s->view->stage->phase==P::Issued&&owner.retryReviewed(6,s->revision)==D::NotReady&&bytes(side(p.context.file,".billing-recovery"))==journal);}
    // Lost confirmation is not acknowledged. Explicit readonly reconstruction can prove the exact stored outcome.
    {auto p=plan(root/"fixture.billing-input-lost.db");auto h=helpers;h.input=inputFixture;Clock clock;BillingOwner owner(h,context(p),policy(),[&]{return clock.get();});CHECK(owner.start(7)&&completed(owner));
        CHECK(owner.createReviewed(7,owner.snapshot()->revision)==D::Queued&&completed(owner));auto s=owner.snapshot();CHECK(s->uncertain&&s->uncertain->manifest&&!s->actionProof&&!s->reviewable);
        CHECK(refresh(owner));s=owner.snapshot();CHECK(!s->uncertain&&s->state==O::Ready&&s->view->stage->outcome&&s->view->monetary->state==R::Empty);}
    // Prepared recovery keeps source bytes frozen, then requires separate explicit stage reconciliation.
    {auto p=plan(root/"fixture.billing-input-prepared.db");const auto created=writer.transact(p,"",cancel,10000);CHECK(created.confirmed.has_value());auto h=helpers;h.input=inputFixture;Clock clock;
        {BillingOwner owner(h,context(p),policy(),[&]{return clock.get();});CHECK(owner.start(8)&&completed(owner));CHECK(owner.submitReviewed(8,owner.snapshot()->revision,billing_fixture::chunk())==D::Queued&&completed(owner));CHECK(static_cast<bool>(owner.snapshot()->uncertain));}
        const auto original=bytes(side(p.context.file)),journal=bytes(side(p.context.file,".billing-recovery"));
        BillingOwner owner(helpers,context(p),policy(),[&]{return clock.get();});CHECK(owner.start(9)&&completed(owner));auto s=owner.snapshot();
        CHECK(s->view->stage->phase==P::Issued&&s->view->monetary->state==R::Prepared&&bytes(side(p.context.file))==original&&bytes(side(p.context.file,".billing-recovery"))==journal);
        CHECK(owner.retryReviewed(9,s->revision)==D::Queued&&completed(owner));CHECK(owner.snapshot()->actionProof&&owner.snapshot()->actionProof->committed.sequence==1&&bytes(side(p.context.file))==original);
        CHECK(refresh(owner));s=owner.snapshot();CHECK(s->view->stage->phase==P::Issued&&s->view->monetary->state==R::Confirmed);
        CHECK(owner.reconcileReviewed(9,s->revision)==D::Queued&&completed(owner)&&refresh(owner));CHECK(owner.snapshot()->state==O::Ready&&inspect(p).committed.sequence==1);}
    {auto p=plan(root/"fixture.billing-input-created.db");auto h=helpers;h.input=inputFixture;Clock clock;BillingOwner owner(h,context(p),policy(),[&]{return clock.get();});CHECK(owner.start(10)&&completed(owner));
        CHECK(owner.createReviewed(10,owner.snapshot()->revision)==D::Queued&&completed(owner)&&refresh(owner));const auto s=owner.snapshot();
        CHECK(s->uncertain&&s->view->stage->phase==P::Issued&&s->view->monetary->state==R::Empty&&owner.createReviewed(10,s->revision)==D::NotReady&&owner.reconcileReviewed(10,s->revision)==D::NotReady);}
    {auto dir=root/"retry-lost";std::filesystem::create_directory(dir);auto p=plan(dir/"fixture.billing-input-prepared.db");CHECK(writer.transact(p,"",cancel,10000).confirmed.has_value());
        auto h=helpers;h.input=inputFixture;h.retry=fixture;Clock clock;BillingOwner owner(h,context(p),policy(),[&]{return clock.get();});CHECK(owner.start(14)&&completed(owner));
        CHECK(owner.submitReviewed(14,owner.snapshot()->revision,billing_fixture::chunk())==D::Queued&&completed(owner)&&refresh(owner));const auto retained=owner.snapshot()->uncertain;CHECK(static_cast<bool>(retained));
        CHECK(owner.retryReviewed(14,owner.snapshot()->revision)==D::Queued&&completed(owner));CHECK(owner.snapshot()->transport==LocalTaskStatus::Unavailable&&owner.snapshot()->uncertain==retained&&!owner.snapshot()->actionProof);
        CHECK(refresh(owner));CHECK(owner.snapshot()->uncertain==retained&&owner.snapshot()->view->monetary->state==R::Confirmed&&owner.snapshot()->view->stage->phase==P::Issued);
        CHECK(owner.reconcileReviewed(14,owner.snapshot()->revision)==D::Queued&&completed(owner)&&refresh(owner));CHECK(!owner.snapshot()->uncertain&&owner.snapshot()->state==O::Ready);}
    // Stop revokes a successful but delayed write acknowledgement; only a new owner may inspect the durable result.
    {auto p=plan(root/"fixture.billing-owner-late.db");auto h=helpers;h.input=fixture;Clock clock;
        {BillingOwner owner(h,context(p),policy(),[&]{return clock.get();});CHECK(owner.start(12)&&completed(owner));CHECK(owner.createReviewed(12,owner.snapshot()->revision)==D::Queued);
            CHECK(until([&]{return std::filesystem::exists(side(p.context.file,".fixture-ready"));}));const auto begin=std::chrono::steady_clock::now();owner.requestStop();
            CHECK(std::chrono::steady_clock::now()-begin<std::chrono::milliseconds(100));const auto stopped=owner.snapshot();put(side(p.context.file,".fixture-release"),"1");owner.stop();
            CHECK(std::chrono::steady_clock::now()-begin<std::chrono::seconds(2)&&owner.snapshot()==stopped&&stopped->uncertain&&!stopped->actionProof&&!stopped->reviewable&&!owner.start(13));}
        BillingOwner owner(helpers,context(p),policy(),[&]{return clock.get();});CHECK(owner.start(13)&&completed(owner)&&owner.snapshot()->state==O::Ready);}
    for(unsigned fault=0;fault<3;++fault){auto p=plan(root/("clock-"+std::to_string(fault)+".db"));Clock clock;BillingOwner owner(helpers,context(p),policy(),[&]{return clock.get();});CHECK(owner.start(20+fault)&&completed(owner));
        clock.advance(fault==0?-1:fault==1?3001:0,fault==2?-1:0);CHECK(until([&]{return owner.snapshot()->state==O::ClockFault;}));CHECK(!owner.snapshot()->reviewable&&!owner.start(30));}
    {auto p=plan(root/"fixture.billing-owner-clock.db");auto h=helpers;h.input=fixture;Clock clock;BillingOwner owner(h,context(p),policy(),[&]{return clock.get();});CHECK(owner.start(31)&&completed(owner));
        CHECK(owner.createReviewed(31,owner.snapshot()->revision)==D::Queued);CHECK(until([&]{return std::filesystem::exists(side(p.context.file,".fixture-ready"));}));clock.advance(-1,0);put(side(p.context.file,".fixture-release"),"1");
        CHECK(until([&]{return owner.snapshot()->state==O::ClockFault;}));CHECK(owner.snapshot()->uncertain&&!owner.snapshot()->actionProof&&!owner.snapshot()->reviewable);}
    // An invalidated read cannot republish; failed/timeout reads never poll or perform writes automatically.
    {auto dir=root/"invalidate";std::filesystem::create_directory(dir);auto p=plan(dir/"fixture.billing-owner-read-hang.db");auto h=helpers;h.monetaryQuery=fixture;Clock clock;
        BillingOwner owner(h,context(p),policy(),[&]{return clock.get();});CHECK(owner.start(40));CHECK(until([&]{return std::filesystem::exists(side(p.context.file,".fixture-ready"));}));
        CHECK(owner.invalidate(40)&&completed(owner));CHECK(!owner.snapshot()->view&&!owner.snapshot()->reviewable);put(side(p.context.file,".fixture-release"),"1");CHECK(refresh(owner)&&owner.snapshot()->view->stable);}
    {auto dir=root/"timeout";std::filesystem::create_directory(dir);auto p=plan(dir/"fixture.billing-owner-read-hang.db");auto h=helpers;h.monetaryQuery=fixture;Clock clock;
        BillingOwner owner(h,context(p),policy(500),[&]{return clock.get();});CHECK(owner.start(41)&&completed(owner));const auto s=owner.snapshot();CHECK(s->transport==LocalTaskStatus::Timeout&&!s->view);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));CHECK(owner.snapshot()==s&&!std::filesystem::exists(p.context.file));}
    // Independently valid earlier monetary facts are a rollback, not permission to reinitialize.
    {auto p=plan(root/"rollback.db");const auto created=writer.transact(p,"",cancel,10000);const auto db=bytes(p.context.file),journal=bytes(side(p.context.file,".billing-recovery")),input=bytes(side(p.context.file));
        p.prior=created.confirmed;p.chunk=billing_fixture::chunk();CHECK(writer.transact(p,created.reply->revision,cancel,10000).confirmed.has_value());Clock clock;
        BillingOwner owner(helpers,context(p),policy(),[&]{return clock.get();});CHECK(owner.start(42)&&completed(owner)&&owner.snapshot()->view->monetary->committed.sequence==1);
        put(p.context.file,db);put(side(p.context.file,".billing-recovery"),journal);put(side(p.context.file),input);CHECK(refresh(owner));
        CHECK(owner.snapshot()->state==O::Faulted&&owner.snapshot()->status==S::Attribution&&!owner.snapshot()->reviewable);}
    {auto p=plan(root/"missing-money.db");CHECK(writer.transact(p,"",cancel,10000).confirmed.has_value());Clock clock;BillingOwner owner(helpers,context(p),policy(),[&]{return clock.get();});CHECK(owner.start(43)&&completed(owner));
        std::filesystem::remove(p.context.file);std::filesystem::remove(side(p.context.file,".billing-recovery"));CHECK(refresh(owner));const auto s=owner.snapshot();
        CHECK(s->view->stage->outcome&&s->view->monetary->status==S::Storage&&s->state==O::NeedsReview&&owner.createReviewed(43,s->revision)==D::NotReady);}
    {auto p=plan(root/"same-sequence.db");{BillingRecoveryStore store;CHECK(store.open(p.context.file,p.context.binding,at,BillingRecoveryOpen::CreateNew)==S::Ok);}
        const auto empty=bytes(p.context.file),emptyJournal=bytes(side(p.context.file,".billing-recovery"));
        {BillingRecoveryStore store;CHECK(store.open(p.context.file,p.context.binding,at)==S::Ok&&store.submit(billing_fixture::chunk(),at)==S::Ok);}
        const auto first=bytes(p.context.file),firstJournal=bytes(side(p.context.file,".billing-recovery"));put(p.context.file,empty);put(side(p.context.file,".billing-recovery"),emptyJournal);
        {BillingRecoveryStore store;auto other=billing_fixture::chunk();other.id="fixture.fork";CHECK(store.open(p.context.file,p.context.binding,at)==S::Ok&&store.submit(other,at)==S::Ok);}
        Clock clock;BillingOwner owner(helpers,context(p),policy(),[&]{return clock.get();});CHECK(owner.start(45)&&completed(owner));put(p.context.file,first);put(side(p.context.file,".billing-recovery"),firstJournal);
        CHECK(refresh(owner)&&owner.snapshot()->state==O::Faulted&&owner.snapshot()->status==S::Attribution);}
    // Invalid and over-budget receipts are refused before admission copies; valid exact receipts survive source removal.
    {auto p=plan(root/"source.db");p.context.binding=binding(root/"power.db");p.context.nowUtcMs=now;CHECK(writer.transact(p,"",cancel,10000).confirmed.has_value());
        CHECK(executeEnergyHistoryRequest(input(p.context.binding,1)).applied==EnergyHistoryStatus::Ok);const auto q=query(p.context.binding,{0,sha256Hex(energy_history_detail::bindingText(p.context.binding.source->binding))});BillingChunk c;
        CHECK(makeBillingSourceChunk(1,q,executeEnergyTraceRequest(q),&c)==S::Ok);Clock clock;clock.value.utcMs=now;BillingOwner owner(helpers,context(p),policy(),[&]{return clock.get();});CHECK(owner.start(44)&&completed(owner));const auto rev=owner.snapshot()->revision;
        for(unsigned i=0;i<7;++i){auto bad=c;if(i==0)bad.trace.intervals.resize(513);if(i==1)bad.receipt->reply.source.checkpoint.batchId=std::string(129,'x');
            if(i==2)bad.receipt->reply.source.recovery->batch->readings.resize(129);
            if(i==3)bad.receipt->reply.source.recovery->priorDigest=std::string(65,'f');
            if(i==4)bad.receipt->reply.source.totals.resize(5);
            if(i==5)bad.receipt->reply.completeTrace->intervals.resize(513);
            if(i==6)bad.receipt->request.source.generation=2;
            CHECK(owner.submitReviewed(44,rev,bad)==D::Invalid&&owner.snapshot()->revision==rev);}
        std::filesystem::remove(p.context.binding.source->file);std::filesystem::remove(journal(p.context.binding.source->file));
        CHECK(owner.submitReviewed(44,rev,c)==D::Queued&&completed(owner)&&refresh(owner));
        CHECK(owner.snapshot()->view->monetary->original&&billing_detail::chunkText(*owner.snapshot()->view->monetary->original)==billing_detail::chunkText(c));}
    std::filesystem::remove_all(root);return finishTests("billing owner");
}
