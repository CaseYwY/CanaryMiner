#include "product/credit_owner.h"
#include "credit_fixture.h"
#include "product_test_util.h"
#include <chrono>
#include <thread>
using namespace om;
using namespace credit_fixture;
namespace {
template<class Predicate> bool until(Predicate predicate) {
    const auto end = std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < end) { if (predicate()) return true; std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
    return predicate();
}
bool ready(CreditLedgerOwner &owner) { return until([&]{auto s=owner.snapshot(); return s->state == CreditOwnerState::Ready || s->state == CreditOwnerState::NeedsReconciliation;}); }
}
int main(int argc, char **argv) {
    CHECK(argc == 3); if (argc != 3) return finishTests("credit owner");
    const auto root = std::filesystem::path(makeScratchDir("credit-owner"));
    const auto helper = std::filesystem::absolute(argv[1]), fixture = std::filesystem::absolute(argv[2]);
    std::atomic<int64_t> now{T}; const auto clock = [&]{return now.load();};
    CreditOwnerPolicy policy; policy.maxJobs = 3; policy.operationTimeoutMs = 3000;
    {
        CreditLedgerOwner unsupported(helper,root/"unsupported.db",{},policy,clock);
        CHECK(!unsupported.start(1) && !std::filesystem::exists(root/"unsupported.db"));
        CHECK(unsupported.snapshot()->state == CreditOwnerState::Stopped);
    }
    {
        CreditLedgerOwner owner(helper,root/"normal.db",contract(),policy,clock);
        CHECK(owner.snapshot()->generation == 0 && owner.snapshot()->state == CreditOwnerState::Stopped);
        CHECK(!owner.start(0) && owner.start(1) && !owner.start(2));
        CHECK(ready(owner)); const auto empty = owner.snapshot();
        CHECK(empty->reply && !empty->reply->totals[0].completePostedNet && empty->reply->totals[0].completeness == CreditCompleteness::Unavailable);
        auto b = batch(1,T,{record("fixture.credit",1,T,"4.25")});
        CHECK(owner.submit(0,b).status == CreditEnqueueStatus::StaleGeneration);
        auto submitted = owner.submit(1,b); CHECK(submitted.status == CreditEnqueueStatus::Queued && submitted.ticket == 2);
        CHECK(until([&]{return owner.snapshot()->lastSuccessfulTicket == submitted.ticket;}));
        CHECK(owner.snapshot()->reply->totals[0].completePostedNet->text() == "4.25");
        CHECK(empty->reply->totals[0].completeness == CreditCompleteness::Unavailable); // prior published object is immutable
        now = T+1000;
        submitted = owner.submit(1,b); CHECK(submitted.status == CreditEnqueueStatus::Queued);
        CHECK(until([&]{return owner.snapshot()->lastSuccessfulTicket == submitted.ticket;}));
        CHECK(owner.snapshot()->reply->applied == CreditLedgerStatus::Replay && owner.snapshot()->reply->totals[0].observedAtMs == T && !owner.snapshot()->reply->totals[0].completePostedNet);
        now = T+500; CHECK(owner.refresh(1).status == CreditEnqueueStatus::Invalid); now = T+2000;
        CHECK(owner.refresh(1,std::vector<CreditQuerySpec>(5)).status == CreditEnqueueStatus::Invalid);
        b = batch(2,T+2000,{record("fixture.credit",2,T,"3")}); submitted = owner.submit(1,b);
        CHECK(submitted.status == CreditEnqueueStatus::Queued && until([&]{return owner.snapshot()->lastSuccessfulTicket == submitted.ticket;}));
        CHECK(owner.snapshot()->reply->totals[0].completePostedNet->text() == "3");
        owner.stop(); CHECK(owner.snapshot()->state == CreditOwnerState::Stopped && owner.snapshot()->pendingJobs == 0 && owner.snapshot()->reservedWireBytes == 0);
        CHECK(owner.submit(1,b).status == CreditEnqueueStatus::NotReady && !owner.start(1));
        CHECK(owner.start(2) && ready(owner));
        CHECK(owner.submit(1,b).status == CreditEnqueueStatus::StaleGeneration);
        CHECK(owner.snapshot()->reply->totals[0].knownPostedNet.text() == "3");
        b = batch(4,T+2000); submitted = owner.submit(2,b); CHECK(submitted.status == CreditEnqueueStatus::Queued);
        CHECK(until([&]{return owner.snapshot()->state == CreditOwnerState::Faulted;}));
        CHECK(owner.snapshot()->reply->applied == CreditLedgerStatus::SequenceGap && !owner.snapshot()->writeOutcomeUnknown);
        CHECK(owner.submit(2,b).status == CreditEnqueueStatus::NotReady); owner.stop();
    }
    now = T;
    {
        CreditOwnerPolicy tiny = policy; tiny.maxWireBytes = 1024;
        CreditLedgerOwner owner(helper,root/"byte-limit.db",contract(),tiny,clock);
        CHECK(owner.start(1) && ready(owner));
        auto b = batch(1,T); for (unsigned i=0;i<20;++i) b.records.push_back(record("fixture.long-"+std::string(80,'x')+std::to_string(i),1,T,"1"));
        const auto before = owner.snapshot()->lastAcceptedTicket;
        CHECK(owner.submit(1,b).status == CreditEnqueueStatus::Full && owner.snapshot()->lastAcceptedTicket == before);
        CHECK(owner.snapshot()->reservedWireBytes == 0); owner.stop();
    }
    {
        const auto file = root/"queue-stop.db";
        CreditLedgerOwner owner(fixture,file,contract(),policy,clock);
        CHECK(owner.start(1) && ready(owner));
        auto b = batch(1,T,{record("fixture.stall-before-apply",1,T,"1")});
        CHECK(owner.submit(1,b).status == CreditEnqueueStatus::Queued);
        CHECK(until([&]{return std::filesystem::exists(file.string()+".fixture-ready");}));
        CHECK(owner.refresh(1).status == CreditEnqueueStatus::Queued);
        CHECK(owner.submit(1,batch(2,T)).status == CreditEnqueueStatus::Queued);
        const auto before = owner.snapshot()->lastAcceptedTicket;
        CHECK(owner.refresh(1).status == CreditEnqueueStatus::Full && owner.snapshot()->lastAcceptedTicket == before);
        auto begin = std::chrono::steady_clock::now(); owner.requestStop();
        CHECK(std::chrono::steady_clock::now()-begin < std::chrono::milliseconds(200));
        const auto stopped = owner.snapshot();
        CHECK(stopped->state == CreditOwnerState::Stopped && stopped->writeOutcomeUnknown && stopped->uncertainSequence == 1 && stopped->droppedJobs == 2);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        CHECK(owner.snapshot()->revision == stopped->revision); // no late worker publication after revocation
        owner.stop(); CHECK(std::chrono::steady_clock::now()-begin < std::chrono::seconds(2));
        CHECK(owner.snapshot()->pendingJobs == 0 && owner.snapshot()->reservedWireBytes == 0);
        CreditProcessClient query(helper); CreditProcessRequest r; r.generation=1; r.ticket=1; r.file=file; r.binding=contract(); r.nowUtcMs=T; r.queries={{true,0,T}}; std::atomic<bool> cancel{false};
        const auto facts = query.transact(r,cancel,5000);
        CHECK(facts.reply && facts.reply->totals[0].matchedRecords == 0 && !facts.reply->totals[0].completePostedNet); // queued work was never silently flushed on stop
    }
    {
        const auto file = root/"timeout-latch.db"; auto timed = policy; timed.operationTimeoutMs = 600;
        CreditLedgerOwner owner(fixture,file,contract(),timed,clock);
        CHECK(owner.start(1) && ready(owner));
        CHECK(owner.submit(1,batch(1,now.load(),{record("fixture.stall-after-apply",1,T,"1.5")})).status == CreditEnqueueStatus::Queued);
        CHECK(until([&]{return owner.snapshot()->state == CreditOwnerState::Faulted;}));
        const auto fault = owner.snapshot();
        CHECK(fault->transport == LocalTaskStatus::Timeout && fault->writeOutcomeUnknown && !fault->reply);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        CHECK(owner.snapshot()->revision == fault->revision && owner.submit(1,batch(2,now.load())).status == CreditEnqueueStatus::NotReady);
        owner.stop();
    }
    {
        const auto file = root/"lost-ack.db";
        CreditLedgerOwner owner(fixture,file,contract(),policy,clock);
        CHECK(owner.start(1) && ready(owner));
        auto b = batch(1,T,{record("fixture.stall-after-apply",1,T,"2.5")});
        CHECK(owner.submit(1,b).status == CreditEnqueueStatus::Queued);
        CHECK(until([&]{return std::filesystem::exists(file.string()+".fixture-ready");}));
        owner.stop(); CHECK(owner.snapshot()->writeOutcomeUnknown);
        now = T+1000; CHECK(owner.start(2) && ready(owner));
        CHECK(owner.snapshot()->state == CreditOwnerState::NeedsReconciliation && owner.snapshot()->reply->totals[0].knownPostedNet.text() == "2.5");
        CHECK(owner.submit(2,batch(2,T+1000)).status == CreditEnqueueStatus::NeedsReconciliation);
        auto retry = owner.retryUncertain(2); CHECK(retry.status == CreditEnqueueStatus::Queued);
        CHECK(until([&]{return owner.snapshot()->lastSuccessfulTicket == retry.ticket;}));
        CHECK(!owner.snapshot()->writeOutcomeUnknown && owner.snapshot()->state == CreditOwnerState::Ready && owner.snapshot()->reply->applied == CreditLedgerStatus::Replay);
        CHECK(owner.snapshot()->reply->totals[0].knownPostedNet.text() == "2.5" && owner.snapshot()->reply->totals[0].observedAtMs == T && !owner.snapshot()->reply->totals[0].completePostedNet);
        auto next = owner.submit(2,batch(2,T+1000,{record("fixture.next",1,T+1000,"1")})); CHECK(next.status == CreditEnqueueStatus::Queued);
        CHECK(until([&]{return owner.snapshot()->lastSuccessfulTicket == next.ticket;}));
        CHECK(owner.snapshot()->reply->totals[0].completePostedNet->text() == "3.5"); owner.stop();
    }
    std::filesystem::remove_all(root);
    return finishTests("credit owner");
}
