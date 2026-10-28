#include "product/credit_recovery.h"
#include "product/credit_owner.h"
#include "product/hash.h"
#include "product/json_dom.h"
#include "credit_fixture.h"
#include "product_test_util.h"
#include <fstream>
#include <iterator>
#include <thread>
#include <chrono>
using namespace om;
using namespace credit_fixture;
namespace {
CreditProcessRequest request(const std::filesystem::path &file, bool write=false) {
    CreditProcessRequest r; r.generation=1; r.ticket=1; r.file=file; r.binding=contract(); r.nowUtcMs=T;
    r.queries={{false,T-day,T},{true,0,T}}; r.recovery=write?CreditRecoveryMode::Submit:CreditRecoveryMode::Inspect;
    if (write) r.batch=batch(1,T,{record("fixture.original",1,T,"2.5")});
    return r;
}
std::filesystem::path journalFile(std::filesystem::path file) { file+=".credit-recovery.json"; return file; }
std::string bytes(const std::filesystem::path &file) { std::ifstream in(file,std::ios::binary); return {std::istreambuf_iterator<char>(in),{}}; }
void put(const std::filesystem::path &file, const std::string &text) { std::ofstream out(file,std::ios::binary); out<<text; }
template<class Predicate> bool until(Predicate predicate) {
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while (std::chrono::steady_clock::now()<end) { if(predicate())return true; std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
    return predicate();
}
bool ready(CreditLedgerOwner &owner) { return until([&]{const auto state=owner.snapshot()->state; return state==CreditOwnerState::Ready || state==CreditOwnerState::NeedsReconciliation;}); }
}
int main(int argc, char **argv) {
    CHECK(argc==3); if(argc!=3)return finishTests("credit journal");
    const auto helper=std::filesystem::absolute(argv[1]), fixture=std::filesystem::absolute(argv[2]);
    const auto root=std::filesystem::path(makeScratchDir("credit-journal"));
    auto r=request(root/std::filesystem::u8path("fixture journal-\xe6\xb5\x8b\xe8\xaf\x95.db"),true); const auto file=journalFile(r.file);
    auto initialize=request(r.file); initialize.recovery=CreditRecoveryMode::Volatile;
    CHECK(executeCreditRequest(initialize).opened==CreditLedgerStatus::Ok);
    {
        CreditLedger ledger; CHECK(ledger.open(r.file,r.binding,T)==CreditLedgerStatus::Ok);
        CHECK(ledger.verifyLatest(*r.batch,T)==CreditLedgerStatus::Conflict && ledger.cumulative(T,T).matchedRecords==0);
    }
    std::string saved,wire; CreditProcessRequest decoded;
    CHECK(encodeCreditRequest(r,&wire) && decodeCreditRequest(wire,&decoded) && decoded.recovery==CreditRecoveryMode::Submit);
    auto invalid=r; invalid.recovery=CreditRecoveryMode::Inspect; CHECK(!encodeCreditRequest(invalid,&wire));
    invalid=r; invalid.batch.reset(); CHECK(!encodeCreditRequest(invalid,&wire));
    invalid=r; invalid.recovery=static_cast<CreditRecoveryMode>(4); CHECK(!encodeCreditRequest(invalid,&wire));
    auto large=r;
    for(unsigned i=0;i<450;++i)large.batch->records.push_back(record("fixture.large-"+std::to_string(i)+std::string(100,'x'),1,T,"1"));
    CHECK(!encodeCreditRequest(large,&wire)); large.recovery=CreditRecoveryMode::Volatile;
    CHECK(encodeCreditRequest(large,&wire) && wire.size()>creditRecoveryRequestLimit && wire.size()<localTaskRequestLimit);
    auto boundary=request(root/"wire-boundary.db",true); boundary.batch->records.clear(); bool limited=false;
    for(unsigned i=0;i<512;++i) {
        boundary.batch->records.push_back(record("fixture.boundary-"+std::string(100,'x')+std::to_string(i),1,T,"1"));
        if(!encodeCreditRequest(boundary,&wire)) { boundary.batch->records.pop_back(); limited=true; break; }
    }
    CHECK(limited && encodeCreditRequest(boundary,&wire) && wire.size()<=creditRecoveryRequestLimit && wire.size()>creditRecoveryRequestLimit-200);
    auto boundaryReply=executeCreditRequest(boundary); CHECK(boundaryReply.applied==CreditLedgerStatus::Ok);
    CHECK(encodeCreditReply(boundary,boundaryReply,&wire) && wire.size()<=localTaskResponseLimit);
    CreditProcessReply boundaryDecoded;
    CHECK(decodeCreditReply(boundary,wire,&boundaryDecoded) && boundaryDecoded.recovery->batch->records.size()==boundary.batch->records.size());
    {
        CreditRecoveryJournal j; CHECK(j.open(r)==CreditLedgerStatus::Ok);
        CHECK(j.snapshot().state==CreditRecoveryState::Empty && !std::filesystem::exists(file));
        CHECK(j.prepare(r)==CreditLedgerStatus::Ok && j.snapshot().state==CreditRecoveryState::Prepared);
        saved=bytes(file); CHECK(!saved.empty() && saved.size()<creditRecoveryRequestLimit+256);
        auto same=r; same.generation=42; same.ticket=99; same.nowUtcMs=T+1000; same.queries={{true,0,T+1000}}; same.recovery=CreditRecoveryMode::Retry;
        CHECK(j.matches(same)); same.batch->records[0].amount=record("fixture.x",1,T,"2.6").amount; CHECK(!j.matches(same));
        CHECK(j.prepare(r)==CreditLedgerStatus::Conflict && bytes(file)==saved);
    }
    {
        CreditRecoveryJournal j; CHECK(j.open(request(r.file))==CreditLedgerStatus::Ok);
        CHECK(j.snapshot().state==CreditRecoveryState::Prepared && j.snapshot().batch->observedAtMs==T);
        CHECK(j.settle(CreditLedgerStatus::Storage)==CreditLedgerStatus::Invalid && bytes(file)==saved);
    }
    auto blocked=r; blocked.batch=batch(2,T,{record("fixture.other",1,T,"9")});
    auto reply=executeCreditRequest(blocked);
    CHECK(reply.applied==CreditLedgerStatus::Conflict && reply.recovery && reply.recovery->state==CreditRecoveryState::Prepared);
    CHECK(reply.recovery->batch->sequence==1 && reply.totals[0].matchedRecords==0 && bytes(file)==saved);
    CHECK(encodeCreditReply(blocked,reply,&wire)); CreditProcessReply roundtrip;
    CHECK(decodeCreditReply(blocked,wire,&roundtrip) && roundtrip.recovery->batch->snapshotId==r.batch->snapshotId);
    auto badReply=reply; badReply.recovery->state=CreditRecoveryState::Confirmed; CHECK(!encodeCreditReply(blocked,badReply,&wire));
    badReply=reply; badReply.recovery->status=CreditLedgerStatus::Storage; CHECK(!encodeCreditReply(blocked,badReply,&wire));
    auto retry=r; retry.recovery=CreditRecoveryMode::Retry; retry.nowUtcMs=T+1000; for(auto &q:retry.queries)q.throughInclusiveMs=T+1000;
    reply=executeCreditRequest(retry);
    CHECK(reply.applied==CreditLedgerStatus::Ok && reply.recovery->state==CreditRecoveryState::Confirmed && reply.totals[0].knownPostedNet.text()=="2.5");
    CHECK(reply.totals[0].observedAtMs==T && !reply.totals[0].completePostedNet && reply.recovery->batch->observedAtMs==T);
    badReply=reply; badReply.recovery->batch->records[0].amount=record("fixture.x",1,T,"9").amount;
    CHECK(!encodeCreditReply(retry,badReply,&wire));
    const auto confirmed=bytes(file);
    const auto stamp=std::filesystem::last_write_time(file);
    auto inspect=request(r.file); inspect.nowUtcMs=T+2000; for(auto &q:inspect.queries)q.throughInclusiveMs=inspect.nowUtcMs;
    reply=executeCreditRequest(inspect);
    CHECK(reply.recovery->state==CreditRecoveryState::Confirmed && bytes(file)==confirmed && std::filesystem::last_write_time(file)==stamp);
    CHECK(reply.totals[0].knownPostedNet.text()=="2.5" && reply.totals[0].observedAtMs==T);
    auto mismatch=retry; mismatch.batch->observedAtMs++; CHECK(executeCreditRequest(mismatch).applied==CreditLedgerStatus::Conflict);
    reply=executeCreditRequest(retry); CHECK(reply.applied==CreditLedgerStatus::Replay && reply.totals[0].knownPostedNet.text()=="2.5");
    auto gap=r; gap.batch=batch(3,T); reply=executeCreditRequest(gap);
    CHECK(reply.applied==CreditLedgerStatus::SequenceGap && reply.recovery->state==CreditRecoveryState::Rejected && reply.recovery->applied==CreditLedgerStatus::SequenceGap);
    auto next=r; next.batch=batch(2,T,{record("fixture.next",1,T,"1")}); reply=executeCreditRequest(next);
    CHECK(reply.applied==CreditLedgerStatus::Ok && reply.recovery->state==CreditRecoveryState::Confirmed && reply.totals[0].knownPostedNet.text()=="3.5");
    {
        CreditRecoveryJournal j; CHECK(j.open(request(r.file))==CreditLedgerStatus::Ok);
        CreditRecoveryJournal competing; CHECK(competing.open(request(r.file))==CreditLedgerStatus::Storage);
    }
    // An older prepared batch is not reconciled merely because an unrelated writer advanced the stream cursor.
    auto stale=request(root/"stale.db",true);
    initialize=request(stale.file); initialize.recovery=CreditRecoveryMode::Volatile; CHECK(executeCreditRequest(initialize).opened==CreditLedgerStatus::Ok);
    { CreditRecoveryJournal j; CHECK(j.open(stale)==CreditLedgerStatus::Ok && j.prepare(stale)==CreditLedgerStatus::Ok); }
    auto bypass=stale; bypass.recovery=CreditRecoveryMode::Volatile;
    CHECK(executeCreditRequest(bypass).applied==CreditLedgerStatus::Ok);
    bypass.batch=batch(2,T); CHECK(executeCreditRequest(bypass).applied==CreditLedgerStatus::Ok);
    stale.recovery=CreditRecoveryMode::Retry; reply=executeCreditRequest(stale);
    CHECK(reply.applied==CreditLedgerStatus::StaleBatch && reply.recovery->state==CreditRecoveryState::Prepared);
    auto conflicted=request(root/"conflicting-retry.db",true);
    initialize=request(conflicted.file); initialize.recovery=CreditRecoveryMode::Volatile; CHECK(executeCreditRequest(initialize).opened==CreditLedgerStatus::Ok);
    { CreditRecoveryJournal j; CHECK(j.open(conflicted)==CreditLedgerStatus::Ok && j.prepare(conflicted)==CreditLedgerStatus::Ok); }
    bypass=conflicted; bypass.recovery=CreditRecoveryMode::Volatile; bypass.batch->records[0].amount=record("fixture.x",1,T,"9").amount;
    CHECK(executeCreditRequest(bypass).applied==CreditLedgerStatus::Ok);
    conflicted.recovery=CreditRecoveryMode::Retry; reply=executeCreditRequest(conflicted);
    CHECK(reply.applied==CreditLedgerStatus::Conflict && reply.recovery->state==CreditRecoveryState::Prepared);
    CHECK(executeCreditRequest(request(conflicted.file)).recovery->state==CreditRecoveryState::Prepared);
    {
        const auto clock=[] {return T+1000;};
        CreditLedgerOwner owner(helper,conflicted.file,contract(),{},clock); CHECK(owner.start(1) && ready(owner));
        CHECK(owner.snapshot()->state==CreditOwnerState::NeedsReconciliation);
        const auto failed=owner.retryUncertain(1); CHECK(failed.status==CreditEnqueueStatus::Queued);
        CHECK(until([&]{return owner.snapshot()->state==CreditOwnerState::Faulted;}));
        CHECK(owner.snapshot()->writeOutcomeUnknown); owner.stop();
    }
    CHECK(executeCreditRequest(request(conflicted.file)).recovery->state==CreditRecoveryState::Prepared);
    // A confirmed sidecar without matching current database facts is not a license to auto-replay or fabricate zero.
    auto diverged=request(root/"diverged.db",true); CHECK(executeCreditRequest(diverged).applied==CreditLedgerStatus::Ok);
    bypass=diverged; bypass.recovery=CreditRecoveryMode::Volatile; bypass.batch=batch(2,T);
    CHECK(executeCreditRequest(bypass).applied==CreditLedgerStatus::Ok);
    reply=executeCreditRequest(request(diverged.file));
    CHECK(reply.opened==CreditLedgerStatus::Ok && reply.recovery->state==CreditRecoveryState::Diverged && reply.recovery->batch->sequence==1);
    CHECK(executeCreditRequest(diverged).applied==CreditLedgerStatus::Conflict);
    {
        const auto clock=[] {return T+1000;};
        CreditLedgerOwner owner(helper,diverged.file,contract(),{},clock); CHECK(owner.start(1) && ready(owner));
        CHECK(owner.snapshot()->state==CreditOwnerState::NeedsReconciliation && owner.snapshot()->recoveryState==CreditRecoveryState::Diverged);
        const auto failed=owner.retryUncertain(1); CHECK(failed.status==CreditEnqueueStatus::Queued);
        CHECK(until([&]{return owner.snapshot()->state==CreditOwnerState::Faulted;}));
        CHECK(owner.snapshot()->writeOutcomeUnknown); owner.stop();
    }
    CHECK(executeCreditRequest(request(diverged.file)).recovery->state==CreditRecoveryState::Diverged);
    std::filesystem::remove(diverged.file);
    CHECK(executeCreditRequest(request(diverged.file)).opened==CreditLedgerStatus::Storage && !std::filesystem::exists(diverged.file));
    // Damage is not a reset or a fabricated empty ledger. Checksummed canonical bytes include state and original request.
    const auto corrupt=r.file.parent_path()/"corrupt.db", corruptFile=journalFile(corrupt);
    auto seed=request(corrupt,true);
    { CreditRecoveryJournal j; CHECK(j.open(seed)==CreditLedgerStatus::Ok && j.prepare(seed)==CreditLedgerStatus::Ok); }
    const auto good=bytes(corruptFile);
    auto tampered=good; const auto amount=tampered.find("2.5"); CHECK(amount!=std::string::npos); tampered.replace(amount,3,"2.6");
    auto spaced=good; const auto inner=spaced.find("[\"CMCR1\","); CHECK(inner!=std::string::npos); spaced.insert(inner+9," ");
    const auto body=spaced.substr(0,spaced.rfind(",\"")); spaced=body+","+jsonQuote(sha256Hex(body))+"]";
    auto newer=good; newer.replace(newer.find("CMCJ1"),5,"CMCJ2");
    for(const auto &text:std::vector<std::string>{"", "[]", good+"\n", good.substr(0,good.size()-1), tampered, spaced, newer, std::string(creditRecoveryRequestLimit+257,' ')}) {
        put(corruptFile,text); auto failed=executeCreditRequest(request(corrupt));
        CHECK(failed.opened==CreditLedgerStatus::Storage && failed.recovery->state==CreditRecoveryState::Unavailable && !failed.recovery->batch);
        CHECK(bytes(corruptFile)==text && !std::filesystem::exists(corrupt));
    }
    put(corruptFile,good);
    CHECK(executeCreditRequest(request(corrupt)).opened==CreditLedgerStatus::Storage && !std::filesystem::exists(corrupt));
    auto binding=request(corrupt); binding.binding.scopeGeneration="synthetic-another-generation";
    CHECK(executeCreditRequest(binding).opened==CreditLedgerStatus::Attribution && bytes(corruptFile)==good);
    binding=request(corrupt); binding.limits.maxRecords--;
    CHECK(executeCreditRequest(binding).opened==CreditLedgerStatus::Attribution && bytes(corruptFile)==good);
#if !defined(_WIN32)
    std::filesystem::remove(corruptFile); const auto target=root/"synthetic-target.json"; put(target,good);
    std::filesystem::create_symlink(target,corruptFile);
    CHECK(executeCreditRequest(request(corrupt)).opened==CreditLedgerStatus::Storage && bytes(target)==good);
#endif
    // Whole owner recreation finds prepared work, without auto-replay. Both pre-commit and post-commit gaps are tested.
    for(const auto &id:{"fixture.stall-prepared","fixture.stall-unsettled"}) {
        const auto path=root/(std::string(id)+".db"); auto source=batch(1,T,{record(id,1,T,"4")});
        CreditOwnerPolicy policy; policy.operationTimeoutMs=3000;
        const auto clock=[] {return T+1000;};
        {
            CreditLedgerOwner owner(fixture,path,contract(),policy,clock);
            CHECK(owner.start(1) && ready(owner)); CHECK(owner.submit(1,source).status==CreditEnqueueStatus::Queued);
            CHECK(until([&]{return std::filesystem::exists(path.string()+".fixture-ready");}));
            owner.stop(); CHECK(owner.snapshot()->writeOutcomeUnknown);
        }
        {
            CreditLedgerOwner owner(helper,path,contract(),policy,clock);
            CHECK(owner.start(1) && ready(owner)); const auto recovered=owner.snapshot();
            CHECK(recovered->state==CreditOwnerState::NeedsReconciliation && recovered->uncertainSequence==1 && recovered->writeOutcomeUnknown);
            CHECK(recovered->reply->recovery->batch->records[0].id==id && recovered->reply->recovery->batch->observedAtMs==T);
            CHECK(recovered->reply->totals[0].knownPostedNet.text()==(std::string(id)=="fixture.stall-prepared"?"0":"4"));
            CHECK(owner.submit(1,batch(2,T)).status==CreditEnqueueStatus::NeedsReconciliation);
            const auto refresh=owner.refresh(1); CHECK(refresh.status==CreditEnqueueStatus::Queued);
            CHECK(until([&]{return owner.snapshot()->lastSuccessfulTicket==refresh.ticket;}));
            CHECK(owner.snapshot()->state==CreditOwnerState::NeedsReconciliation);
            const auto replay=owner.retryUncertain(1); CHECK(replay.status==CreditEnqueueStatus::Queued);
            CHECK(until([&]{return owner.snapshot()->lastSuccessfulTicket==replay.ticket;}));
            CHECK(owner.snapshot()->state==CreditOwnerState::Ready && !owner.snapshot()->writeOutcomeUnknown && owner.snapshot()->recoveryState==CreditRecoveryState::Confirmed);
            CHECK(owner.snapshot()->reply->applied==(std::string(id)=="fixture.stall-prepared"?CreditLedgerStatus::Ok:CreditLedgerStatus::Replay));
            CHECK(owner.snapshot()->reply->totals[0].knownPostedNet.text()=="4" && owner.snapshot()->reply->totals[0].observedAtMs==T);
            CHECK(recovered->state==CreditOwnerState::NeedsReconciliation && recovered->writeOutcomeUnknown); owner.stop();
        }
        {
            CreditLedgerOwner owner(helper,path,contract(),policy,clock); CHECK(owner.start(1) && ready(owner));
            CHECK(owner.snapshot()->state==CreditOwnerState::Ready && owner.snapshot()->durableSequence==1 && !owner.snapshot()->writeOutcomeUnknown);
            CHECK(owner.snapshot()->reply->totals[0].knownPostedNet.text()=="4"); owner.stop();
        }
    }
    std::filesystem::remove_all(root);
    return finishTests("credit journal");
}
