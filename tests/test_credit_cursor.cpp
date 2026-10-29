// Synthetic source pages only: atomic facts/progress, strict IPC, recovery and one-page owner admission.
#include "product/credit_recovery.h"
#include "product/credit_review.h"
#include "product/hash.h"
#include "credit_fixture.h"
#include "product_test_util.h"
#include <chrono>
#include <fstream>
#include <iterator>
#include <sstream>
#include <thread>
using namespace om;
using namespace credit_fixture;
namespace {
CreditProcessRequest request(const std::filesystem::path &file,std::optional<CreditBatch> b={}) {
    CreditProcessRequest r;r.generation=r.ticket=1;r.file=file;r.binding=cursorContract();r.nowUtcMs=T+10000;
    r.queries={{false,T-day,T+10000},{true,0,T+10000}};r.batch=std::move(b);r.recovery=r.batch?CreditRecoveryMode::Submit:CreditRecoveryMode::Inspect;return r;
}
std::filesystem::path journal(std::filesystem::path file){file+=".credit-recovery.json";return file;}
std::string bytes(const std::filesystem::path &file){std::ifstream in(file,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
void put(const std::filesystem::path &file,const std::string &s){std::ofstream out(file,std::ios::binary);out<<s;}
std::string replace(std::string s,const std::string &a,const std::string &b){const auto at=s.find(a);CHECK(at!=std::string::npos);if(at!=std::string::npos)s.replace(at,a.size(),b);return s;}
template<class P>bool until(P predicate){const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);while(std::chrono::steady_clock::now()<end){if(predicate())return true;std::this_thread::sleep_for(std::chrono::milliseconds(2));}return predicate();}
bool ready(CreditLedgerOwner &o){return until([&]{const auto s=o.snapshot()->state;return s==CreditOwnerState::Ready||s==CreditOwnerState::NeedsReconciliation;});}
void checkpoint(CreditLedger &ledger,uint64_t sequence,const std::string &next,int64_t observed){
    const auto c=ledger.cursor(T+10000);CHECK(c.status==CreditLedgerStatus::Ok&&c.sequence==sequence&&c.next==next&&c.observedAtMs==observed);
    CHECK(c.snapshotId==(sequence?"synthetic-snapshot-"+std::to_string(sequence):""));
}
bool has(const CreditReviewPanel &panel,const std::string &needle){for(const auto &line:panel.lines())if(line.find(needle)!=std::string::npos)return true;return false;}
void frame(const CreditReviewPanel &p,int columns,int rows){std::istringstream in(p.render(columns,rows,ColorLevel::None));std::string line;int count=0;while(std::getline(in,line)){CHECK(line.size()<static_cast<size_t>(columns)&&line.find('\x1b')==std::string::npos);++count;}CHECK(count<=rows);}
}
int main(int argc,char **argv){
    CHECK(argc==3);if(argc!=3)return finishTests("credit cursor");
    const auto helper=std::filesystem::absolute(argv[1]),fixture=std::filesystem::absolute(argv[2]);
    const auto root=std::filesystem::path(makeScratchDir("credit-cursor"));
    const auto first=page(1,T,{record("fixture.credit",1,T,"4.25")});
    for(const auto &s:std::vector<std::string>{"",std::string(513,'x'),"space here","\x1b[2J","\t",std::string(1,char(127)),"\xc3\xa9"})CHECK(!validCreditCursorToken(s));
    CHECK(validCreditCursorToken("!~\"\\%?[]{}")&&validCreditCursorToken(std::string(512,'x')));
    auto binding=cursorContract();binding.cursor->version="real.unreviewed";CHECK(!validCreditSourceContract(binding));
    binding=cursorContract();binding.cursor->initial="";CHECK(!validCreditSourceContract(binding));
    {
        CreditLedger ledger;CHECK(ledger.open(root/"ledger.db",cursorContract(),T+10000)==CreditLedgerStatus::Ok);
        checkpoint(ledger,0,"fixture:initial",0);CHECK(!ledger.cumulative(T,T).completePostedNet);
        auto bad=first;bad.cursor.reset();CHECK(ledger.apply(bad,T)==CreditLedgerStatus::Invalid);
        bad=first;bad.cursor->before="wrong";CHECK(ledger.apply(bad,T)==CreditLedgerStatus::Invalid);
        bad=first;bad.cursor->after=bad.cursor->before;CHECK(ledger.apply(bad,T)==CreditLedgerStatus::Invalid);
        CHECK(ledger.apply(first,T)==CreditLedgerStatus::Ok);checkpoint(ledger,1,"fixture:page-1",T);
        auto replay=first;replay.observedAtMs=T+1000;CHECK(ledger.apply(replay,T+1000)==CreditLedgerStatus::Replay);
        CHECK(ledger.verifyLatest(replay,T+1000)==CreditLedgerStatus::Replay);checkpoint(ledger,1,"fixture:page-1",T);
        bad=first;bad.cursor->after="different";CHECK(ledger.apply(bad,T)==CreditLedgerStatus::Conflict);
        CHECK(ledger.apply(page(3,T),T)==CreditLedgerStatus::SequenceGap);
        bad=page(2,T);bad.cursor->before="not-the-committed-cursor";CHECK(ledger.apply(bad,T)==CreditLedgerStatus::Conflict);
        bad=page(2,T);bad.cursor->after="fixture:initial";CHECK(ledger.apply(bad,T)==CreditLedgerStatus::Invalid);
        // Failure after inserting the snapshot and an earlier sorted record rolls back BOTH facts and progress.
        bad=page(2,T,{record("fixture.aaa-new",1,T,"1"),record("fixture.credit",1,T,"9")});
        CHECK(ledger.apply(bad,T)==CreditLedgerStatus::Conflict);checkpoint(ledger,1,"fixture:page-1",T);
        CHECK(ledger.cumulative(T,T).matchedRecords==1&&ledger.cumulative(T,T).knownPostedNet.text()=="4.25");
        CHECK(ledger.apply(page(2,T+1000),T+1000)==CreditLedgerStatus::Ok);checkpoint(ledger,2,"fixture:page-2",T+1000);
        CHECK(ledger.cumulative(T+1000,T+1000).completeness==CreditCompleteness::Partial); // empty page is not complete income
        bad=page(3,T+2000);bad.cursor->after="fixture:page-1";CHECK(ledger.apply(bad,T+2000)==CreditLedgerStatus::Conflict);
        auto correction=page(3,T+2000,{record("fixture.credit",2,T,"3.75")});correction.coverage={{binding.historyStartMs-1,T+2000}};
        CHECK(ledger.apply(correction,T+2000)==CreditLedgerStatus::Ok);checkpoint(ledger,3,"fixture:page-3",T+2000);
        CHECK(ledger.cumulative(T+2000,T+2000).completePostedNet->text()=="3.75");
        CHECK(ledger.apply(first,T+2000)==CreditLedgerStatus::StaleBatch);checkpoint(ledger,3,"fixture:page-3",T+2000);
        const auto future=ledger.cursor(T);CHECK(future.status==CreditLedgerStatus::Clock&&future.next.empty()&&!future.sequence);
    }
    {
        CreditLedger reopened;CHECK(reopened.open(root/"ledger.db",cursorContract(),T+10000)==CreditLedgerStatus::Ok);checkpoint(reopened,3,"fixture:page-3",T+2000);
    }
    {
        CreditLedger empty;CHECK(empty.open(root/"empty-page.db",cursorContract(),T)==CreditLedgerStatus::Ok&&empty.apply(page(1,T),T)==CreditLedgerStatus::Ok);
        checkpoint(empty,1,"fixture:page-1",T);const auto facts=empty.cumulative(T,T);
        CHECK(facts.completeness==CreditCompleteness::Unavailable&&!facts.completePostedNet); // progress alone cannot assert zero income
    }
    {
        CreditLedgerLimits limits;limits.maxRecords=limits.maxVersions=1;CreditLedger ledger;
        CHECK(ledger.open(root/"capacity.db",cursorContract(),T,limits)==CreditLedgerStatus::Ok&&ledger.apply(first,T)==CreditLedgerStatus::Ok);
        CHECK(ledger.apply(page(2,T,{record("fixture.other",1,T,"1")}),T)==CreditLedgerStatus::Capacity);checkpoint(ledger,1,"fixture:page-1",T);
    }
    {
        CreditLedger legacy;CHECK(legacy.open(root/"legacy.db",contract(),T)==CreditLedgerStatus::Ok);
        CHECK(legacy.cursor(T).status==CreditLedgerStatus::Unsupported&&legacy.apply(batch(1,T),T)==CreditLedgerStatus::Ok);
    }
    const auto legacyBytes=bytes(root/"legacy.db"),cursorBytes=bytes(root/"ledger.db");
    {CreditLedger l;CHECK(l.open(root/"legacy.db",cursorContract(),T)==CreditLedgerStatus::Storage);}
    {CreditLedger l;CHECK(l.open(root/"ledger.db",contract(),T+10000)==CreditLedgerStatus::Storage);}
    CHECK(bytes(root/"legacy.db")==legacyBytes&&bytes(root/"ledger.db")==cursorBytes);
    binding=cursorContract();binding.cursor->version="fixture.other-v1";
    {CreditLedger l;CHECK(l.open(root/"ledger.db",binding,T+10000)==CreditLedgerStatus::Attribution);}
    binding=cursorContract();binding.cursor->initial="other-initial";
    {CreditLedger l;CHECK(l.open(root/"ledger.db",binding,T+10000)==CreditLedgerStatus::Attribution);}
    // Request/reply dialects and maximum byte lengths, including escaped opaque tokens.
    auto r=request(root/"wire.db",first);r.batch->cursor->after=std::string(500,'x')+"\"\\!?[]{}";
    std::string wire,original;CreditProcessRequest decoded;CreditProcessReply decodedReply;
    CHECK(encodeCreditRequest(r,&original)&&original.find("CMCR3")!=std::string::npos&&decodeCreditRequest(original,&decoded));
    CHECK(decoded.batch->cursor->after==r.batch->cursor->after&&sameCreditSourceContract(decoded.binding,r.binding));
    for(const auto mode:{CreditRecoveryMode::Volatile,CreditRecoveryMode::Submit,CreditRecoveryMode::Retry}){auto a=r;a.recovery=mode;CHECK(encodeCreditRequest(a,&wire)&&decodeCreditRequest(wire,&decoded)&&decoded.recovery==mode);}
    auto invalid=r;invalid.batch->cursor.reset();CHECK(!encodeCreditRequest(invalid,&wire));
    invalid=r;invalid.binding.cursor.reset();invalid.batch->binding=invalid.binding;CHECK(!encodeCreditRequest(invalid,&wire));
    for(const auto &bad:std::vector<std::string>{replace(original,"CMCR3","CMCR2"),replace(original,"CMCR3","CMCR4"),original+"x",original.substr(0,original.size()-1)})CHECK(!decodeCreditRequest(bad,&decoded));
    CHECK(decodeCreditRequest(original+std::string(creditRecoveryRequestLimit-original.size(),' '),&decoded));
    CHECK(!decodeCreditRequest(original+std::string(creditRecoveryRequestLimit+1-original.size(),' '),&decoded));
    auto result=executeCreditRequest(r);CHECK(result.applied==CreditLedgerStatus::Ok&&result.cursor&&result.cursor->sequence==1&&result.cursor->next==r.batch->cursor->after);
    CHECK(encodeCreditReply(r,result,&wire)&&wire.find("CMCA3")!=std::string::npos&&decodeCreditReply(r,wire,&decodedReply));
    CHECK(decodeCreditReply(r,wire+std::string(localTaskResponseLimit-wire.size(),' '),&decodedReply));
    CHECK(!decodeCreditReply(r,wire+std::string(localTaskResponseLimit+1-wire.size(),' '),&decodedReply));
    CHECK(!decodeCreditReply(r,replace(wire,"CMCA3","CMCA2"),&decodedReply));
    auto forged=result;forged.cursor->next="forged-next";CHECK(!encodeCreditReply(r,forged,&wire));
    forged=result;forged.cursor->sequence++;CHECK(!encodeCreditReply(r,forged,&wire));
    forged=result;forged.cursor->observedAtMs++;CHECK(!encodeCreditReply(r,forged,&wire));
    forged=result;forged.cursor->snapshotId="bad id";CHECK(!encodeCreditReply(r,forged,&wire));
    forged=result;forged.cursor->status=CreditLedgerStatus::Storage;CHECK(!encodeCreditReply(r,forged,&wire));
    forged=result;forged.cursor.reset();CHECK(!encodeCreditReply(r,forged,&wire));
    auto maximum=request(root/"maximum.db",page(1,T));bool limited=false;
    for(unsigned i=0;i<512;++i){maximum.batch->records.push_back(record("fixture.long-"+std::string(100,'x')+std::to_string(i),1,T,"1"));if(!encodeCreditRequest(maximum,&wire)){maximum.batch->records.pop_back();limited=true;break;}}
    CHECK(limited&&encodeCreditRequest(maximum,&wire)&&wire.size()<=creditRecoveryRequestLimit&&wire.size()>creditRecoveryRequestLimit-200);
    result=executeCreditRequest(maximum);CHECK(result.applied==CreditLedgerStatus::Ok&&encodeCreditReply(maximum,result,&wire)&&wire.size()<=localTaskResponseLimit);
    // Journal retains exact before/after; a claimed next token is never a committed checkpoint.
    auto prepared=request(root/"prepared.db",first);CHECK(executeCreditRequest(request(prepared.file)).opened==CreditLedgerStatus::Ok);
    {CreditRecoveryJournal j;CHECK(j.open(prepared)==CreditLedgerStatus::Ok&&j.prepare(prepared)==CreditLedgerStatus::Ok);
        auto changed=prepared;changed.batch->cursor->after="changed";CHECK(!j.matches(changed));changed=prepared;changed.batch->cursor->before="changed";CHECK(!j.matches(changed));}
    const auto pending=bytes(journal(prepared.file));CHECK(pending.find("CMCJ2")!=std::string::npos&&pending.find("CMCR3")!=std::string::npos);
    result=executeCreditRequest(request(prepared.file));CHECK(result.recovery->state==CreditRecoveryState::Prepared&&result.cursor->sequence==0&&result.cursor->next=="fixture:initial");
    CHECK(bytes(journal(prepared.file))==pending&&!result.totals[0].completePostedNet);
    auto retry=prepared;retry.recovery=CreditRecoveryMode::Retry;result=executeCreditRequest(retry);
    CHECK(result.applied==CreditLedgerStatus::Ok&&result.cursor->sequence==1&&result.cursor->observedAtMs==T&&result.recovery->state==CreditRecoveryState::Confirmed);
    const auto settled=bytes(journal(prepared.file));const auto stamp=std::filesystem::last_write_time(journal(prepared.file));
    result=executeCreditRequest(request(prepared.file));CHECK(result.cursor->sequence==1&&bytes(journal(prepared.file))==settled&&std::filesystem::last_write_time(journal(prepared.file))==stamp);
    result=executeCreditRequest(retry);CHECK(result.applied==CreditLedgerStatus::Replay&&result.cursor->observedAtMs==T);
    auto noncanonical=replace(settled,"[\"CMCR3\",","[\"CMCR3\", ");const auto body=noncanonical.substr(0,noncanonical.rfind(",\""));noncanonical=body+","+jsonQuote(sha256Hex(body))+"]";
    for(const auto &bad:std::vector<std::string>{replace(settled,"fixture:page-1","fixture:page-2"),replace(settled,"CMCJ2","CMCJ1"),noncanonical,settled+"\n",std::string(creditRecoveryRequestLimit+257,' ')}){
        put(journal(prepared.file),bad);result=executeCreditRequest(request(prepared.file));CHECK(result.opened==CreditLedgerStatus::Storage&&result.cursor->status==CreditLedgerStatus::Storage&&bytes(journal(prepared.file))==bad);
    }
    put(journal(prepared.file),settled);auto wrong=request(prepared.file);wrong.binding.cursor->version="fixture.changed";
    CHECK(executeCreditRequest(wrong).opened==CreditLedgerStatus::Attribution&&bytes(journal(prepared.file))==settled);
    std::filesystem::remove(journal(prepared.file));result=executeCreditRequest(request(prepared.file));
    CHECK(result.opened==CreditLedgerStatus::Storage&&result.recovery->state==CreditRecoveryState::Unavailable&&!std::filesystem::exists(journal(prepared.file)));
    put(journal(prepared.file),settled);std::filesystem::remove(prepared.file);result=executeCreditRequest(request(prepared.file));
    CHECK(result.opened==CreditLedgerStatus::Storage&&result.recovery->state==CreditRecoveryState::Diverged&&!std::filesystem::exists(prepared.file));
    // Single-page admission races are guarded atomically by generation AND snapshot revision.
    const auto clock=[]{return T+10000;};CreditOwnerPolicy policy;policy.operationTimeoutMs=3000;
    {auto volatilePolicy=policy;volatilePolicy.persistentRecovery=false;CreditLedgerOwner o(helper,root/"volatile.db",cursorContract(),volatilePolicy,clock);CHECK(!o.start(1)&&!std::filesystem::exists(root/"volatile.db"));}
    {
        CreditLedgerOwner o(helper,root/"owner.db",cursorContract(),policy,clock);CHECK(o.start(1)&&ready(o));auto initial=o.snapshot();
        CHECK(initial->state==CreditOwnerState::Ready&&initial->reply->cursor->sequence==0);
        CHECK(o.submit(1,first).status==CreditEnqueueStatus::Invalid&&o.retryUncertain(1).status==CreditEnqueueStatus::Invalid);
        CHECK(o.submitPage(0,initial->revision,first).status==CreditEnqueueStatus::StaleGeneration);
        CHECK(o.submitPage(1,initial->revision+1,first).status==CreditEnqueueStatus::ReviewChanged);
        CHECK(o.submitPage(1,initial->revision,page(2,T)).status==CreditEnqueueStatus::Invalid);
        std::atomic<unsigned> queued{0};std::atomic<bool> go{false};std::vector<std::thread> contenders;
        for(int i=0;i<8;++i)contenders.emplace_back([&]{while(!go.load())std::this_thread::yield();if(o.submitPage(1,initial->revision,first).status==CreditEnqueueStatus::Queued)++queued;});
        go=true;for(auto &t:contenders)t.join();CHECK(queued==1&&until([&]{return o.snapshot()->lastSuccessfulTicket==2;}));
        CHECK(initial->reply->cursor->sequence==0&&o.snapshot()->reply->cursor->sequence==1&&o.snapshot()->lastAcceptedTicket==2);
        const auto saved=o.snapshot();const auto refresh=o.refresh(1);CHECK(refresh.status==CreditEnqueueStatus::Queued&&until([&]{return o.snapshot()->lastSuccessfulTicket==refresh.ticket;}));
        CHECK(o.submitPage(1,saved->revision,page(2,T)).status==CreditEnqueueStatus::ReviewChanged);
        const auto next=o.submitPage(1,o.snapshot()->revision,page(2,T+1000));CHECK(next.status==CreditEnqueueStatus::Queued&&until([&]{return o.snapshot()->lastSuccessfulTicket==next.ticket;}));
        CHECK(o.snapshot()->reply->cursor->sequence==2&&o.snapshot()->reply->totals[0].knownPostedNet.text()=="4.25");
        auto loop=page(3,T+2000);loop.cursor->after="fixture:page-1";
        CHECK(o.submitPage(1,o.snapshot()->revision,loop).status==CreditEnqueueStatus::Queued&&until([&]{return o.snapshot()->state==CreditOwnerState::Faulted;}));
        CHECK(o.snapshot()->reply->cursor->sequence==2&&o.snapshot()->reply->recovery->state==CreditRecoveryState::Rejected&&!o.snapshot()->writeOutcomeUnknown);
        CHECK(o.submitPage(1,o.snapshot()->revision,page(3,T+2000)).status==CreditEnqueueStatus::NotReady);o.stop();
        CHECK(!o.start(1)&&o.start(2)&&ready(o));CHECK(o.snapshot()->reply->cursor->sequence==2);
        CHECK(o.submitPage(1,o.snapshot()->revision,page(3,T+2000)).status==CreditEnqueueStatus::StaleGeneration);o.stop();
        CHECK(o.submitPage(2,o.snapshot()->revision,page(3,T+2000)).status==CreditEnqueueStatus::NotReady);
    }
    // Actual helper interruption before/after COMMIT and a corrupt acknowledgement. Reopening only inspects.
    for(const auto &id:{"fixture.stall-prepared","fixture.stall-unsettled","fixture.stall-after-apply","fixture.bad-ack"}){
        const auto file=root/(std::string(id)+".db");auto source=page(1,T,{record(id,1,T,"2.5")});
        const bool badAck=std::string(id)=="fixture.bad-ack",uncommitted=std::string(id)=="fixture.stall-prepared";
        const bool pending=uncommitted||std::string(id)=="fixture.stall-unsettled";
        {
            CreditLedgerOwner o(fixture,file,cursorContract(),policy,clock);CHECK(o.start(1)&&ready(o));const auto initial=o.snapshot();
            CHECK(o.submitPage(1,initial->revision,source).status==CreditEnqueueStatus::Queued);
            if(badAck)CHECK(until([&]{return o.snapshot()->state==CreditOwnerState::Faulted;}));
            else{CHECK(until([&]{return std::filesystem::exists(file.string()+".fixture-ready");}));
                CHECK(o.snapshot()->state==CreditOwnerState::Busy&&o.snapshot()->reply->cursor->sequence==0);
                CHECK(o.refresh(1).status==CreditEnqueueStatus::Full&&o.submitPage(1,o.snapshot()->revision,page(2,T)).status==CreditEnqueueStatus::NotReady);}
            const auto begin=std::chrono::steady_clock::now();o.requestStop();CHECK(std::chrono::steady_clock::now()-begin<std::chrono::milliseconds(200));
            const auto stopped=o.snapshot();CHECK(stopped->writeOutcomeUnknown&&stopped->uncertainBatch&&stopped->uncertainBatch->cursor->after==source.cursor->after);
            o.stop();CHECK(o.snapshot()->state==CreditOwnerState::Stopped&&o.snapshot()->pendingJobs==0&&std::chrono::steady_clock::now()-begin<std::chrono::seconds(2));
        }
        const auto retained=bytes(journal(file));
        {
            CreditLedgerOwner o(helper,file,cursorContract(),policy,clock);CHECK(o.start(1)&&ready(o));const auto recovered=o.snapshot();
            CHECK(recovered->reply->cursor->sequence==(uncommitted?0:1)&&recovered->reply->totals[0].knownPostedNet.text()==(uncommitted?"0":"2.5"));
            CHECK(bytes(journal(file))==retained&&recovered->state==(pending?CreditOwnerState::NeedsReconciliation:CreditOwnerState::Ready));
            if(pending){
                CHECK(recovered->uncertainBatch&&recovered->uncertainBatch->observedAtMs==T&&recovered->uncertainBatch->cursor->after==source.cursor->after);
                CHECK(o.submitPage(1,recovered->revision,page(2,T)).status==CreditEnqueueStatus::NotReady);
                const auto submitted=o.retryReviewed(1,recovered->revision);CHECK(submitted.status==CreditEnqueueStatus::Queued);
                CHECK(o.retryReviewed(1,recovered->revision).status==CreditEnqueueStatus::ReviewChanged);
                CHECK(until([&]{return o.snapshot()->lastSuccessfulTicket==submitted.ticket;}));
                CHECK(o.snapshot()->reply->applied==(uncommitted?CreditLedgerStatus::Ok:CreditLedgerStatus::Replay)&&!o.snapshot()->writeOutcomeUnknown);
            }
            CHECK(o.snapshot()->reply->cursor->sequence==1&&o.snapshot()->reply->cursor->observedAtMs==T);o.stop();
        }
    }
    // A later independent stream write does not establish that old uncertain content committed.
    auto stale=request(root/"stale.db",first);CHECK(executeCreditRequest(request(stale.file)).opened==CreditLedgerStatus::Ok);
    {CreditRecoveryJournal j;CHECK(j.open(stale)==CreditLedgerStatus::Ok&&j.prepare(stale)==CreditLedgerStatus::Ok);}
    auto direct=stale;direct.recovery=CreditRecoveryMode::Volatile;CHECK(executeCreditRequest(direct).applied==CreditLedgerStatus::Ok);
    direct.batch=page(2,T+1000);CHECK(executeCreditRequest(direct).applied==CreditLedgerStatus::Ok);
    {CreditLedgerOwner o(helper,stale.file,cursorContract(),policy,clock);CHECK(o.start(1)&&ready(o));const auto s=o.snapshot();
        CHECK(s->state==CreditOwnerState::NeedsReconciliation&&s->reply->cursor->sequence==2);
        CHECK(o.retryReviewed(1,s->revision).status==CreditEnqueueStatus::Queued&&until([&]{return o.snapshot()->state==CreditOwnerState::Faulted;}));
        CHECK(o.snapshot()->writeOutcomeUnknown&&o.snapshot()->reply->applied==CreditLedgerStatus::StaleBatch&&o.snapshot()->recoveryState==CreditRecoveryState::Prepared);o.stop();}
    // Privacy and long-token review: no truncation of the exact original opaque cursors.
    auto snapshot=std::make_shared<CreditOwnerSnapshot>();snapshot->binding=cursorContract();snapshot->generation=snapshot->revision=1;
    snapshot->state=CreditOwnerState::NeedsReconciliation;snapshot->persistentRecovery=snapshot->writeOutcomeUnknown=true;snapshot->recoveryState=CreditRecoveryState::Prepared;
    auto privatePage=page(1,T);privatePage.cursor->after=std::string(508,'x')+"TAIL";snapshot->uncertainBatch=std::make_shared<const CreditBatch>(privatePage);
    CreditProcessReply view;view.cursor=CreditCursorCheckpoint{CreditLedgerStatus::Ok,0,"",snapshot->binding.cursor->initial,0,0};snapshot->reply=std::move(view);
    CreditReviewPanel panel;panel.open(snapshot);CHECK(!has(panel,"fixture:initial")&&!has(panel,"TAIL")&&has(panel,"Committed source checkpoint: Ok | sequence 0"));
    panel.key('i');CHECK(has(panel,"Original cursor AFTER: "+privatePage.cursor->after)&&has(panel,"Committed next cursor: fixture:initial"));
    for(int columns:{60,80,120})for(int rows:{12,24}){frame(panel,columns,rows);panel.offset=10000;frame(panel,columns,rows);panel.offset=0;}
    panel.key('i');CHECK(!has(panel,"TAIL")&&!has(panel,"fixture:initial"));
    std::filesystem::remove_all(root);return finishTests("credit cursor");
}
