#include "product/credit_review.h"
#include "product/credit_recovery.h"
#include "product/settings.h"
#include "credit_fixture.h"
#include "product_test_util.h"
#include <chrono>
#include <fstream>
#include <thread>
using namespace om;
using namespace credit_fixture;
namespace {
template<class P>bool until(P predicate){const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);while(std::chrono::steady_clock::now()<deadline){if(predicate())return true;std::this_thread::sleep_for(std::chrono::milliseconds(5));}return predicate();}
bool ready(CreditLedgerOwner &owner){return until([&]{const auto s=owner.snapshot()->state;return s==CreditOwnerState::Ready||s==CreditOwnerState::NeedsReconciliation;});}
std::string bytes(const std::filesystem::path &file){std::ifstream in(file,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
bool has(const CreditReviewPanel &panel,const std::string &needle){for(const auto &line:panel.lines())if(line.find(needle)!=std::string::npos)return true;return false;}
CreditProcessRequest prepared(const std::filesystem::path &path){CreditProcessRequest r;r.generation=r.ticket=1;r.file=path;r.binding=contract();r.nowUtcMs=T;
    r.queries={{true,0,T}};CHECK(executeCreditRequest(r).opened==CreditLedgerStatus::Ok);
    r.recovery=CreditRecoveryMode::Submit;r.batch=batch(1,T,{record("fixture.private-original",1,T,"2.5")});
    CreditRecoveryJournal j;CHECK(j.open(r)==CreditLedgerStatus::Ok&&j.prepare(r)==CreditLedgerStatus::Ok);return r;}
CreditReviewAccess access(CreditLedgerOwner &owner){CreditReviewAccess a;a.snapshot=[&]{return owner.snapshot();};a.inspect=[&](uint64_t g){return owner.refresh(g);};a.retry=[&](uint64_t g,uint64_t r){return owner.retryReviewed(g,r);};return a;}
}
int main(int argc,char **argv){
    CHECK(argc==3);if(argc!=3)return finishTests("credit review");
    const auto helper=std::filesystem::absolute(argv[1]),fixture=std::filesystem::absolute(argv[2]);
    CreditReviewPanel panel;panel.open();unsigned retries=0,inspections=0;
    CreditReviewAccess mock;mock.inspect=[&](uint64_t){++inspections;return CreditEnqueueResult{CreditEnqueueStatus::Queued,5};};
    mock.retry=[&](uint64_t g,uint64_t r){CHECK(g==4&&r==9);++retries;return CreditEnqueueResult{CreditEnqueueStatus::Queued,6};};
    CHECK(has(panel,"no qualified local credit collector")&&!panel.reviewing);
    for(int key:std::vector<int>{'r','f','i',' ',13,kKeyF2})CHECK(panel.key(key,mock)==CreditReviewResult::None);
    CHECK(retries==0&&inspections==0);
    auto s=std::make_shared<CreditOwnerSnapshot>();s->binding=contract();s->generation=4;s->revision=9;s->persistentRecovery=true;
    s->state=CreditOwnerState::NeedsReconciliation;s->recoveryState=CreditRecoveryState::Prepared;s->writeOutcomeUnknown=true;
    s->uncertainBatch=std::make_shared<const CreditBatch>(batch(1,T,{record("fixture.private-original",1,T,"2.5")}));
    s->uncertainSequence=s->durableSequence=1;s->uncertainSnapshotId=s->durableSnapshotId=s->uncertainBatch->snapshotId;
    s->lastAcceptedTicket=s->lastCompletedTicket=s->lastSuccessfulTicket=1;
    panel.open(s);CHECK(!has(panel,"fixture.private-original")&&!has(panel,s->binding.scopeId));
    panel.key('r',mock);CHECK(panel.reviewing&&!panel.acknowledged);panel.key(13,mock);CHECK(retries==0);
    panel.key(' ',mock);CHECK(!panel.acknowledged);panel.key('i',mock);CHECK(has(panel,"fixture.private-original")&&has(panel,"replacement 2.5 PRL"));
    CHECK(has(panel,"source-as-of UTC ms "+std::to_string(T))&&has(panel,"not refreshed")&&has(panel,"empty coverage means unknown"));
    panel.key(' ',mock);CHECK(panel.acknowledged);panel.key(13,mock);CHECK(retries==1&&panel.submittedTicket==6&&!panel.reviewing&&panel.message.find("NOT a durable")!=std::string::npos);
    for(int key:std::vector<int>{13,13,kKeyF2,' ',13})panel.key(key,mock);
    CHECK(retries==1); // buffered Enter cannot replay a second time
    panel.key('r',mock);panel.key(' ',mock);auto changed=std::make_shared<CreditOwnerSnapshot>(*s);changed->revision++;
    panel.update(changed);CHECK(!panel.reviewing&&!panel.acknowledged&&panel.message.find("Owner changed")!=std::string::npos);panel.key(13,mock);CHECK(retries==1);
    panel.open(s);panel.key('r',mock);panel.key('i',mock);panel.key(' ',mock);panel.key('i',mock);CHECK(!panel.acknowledged);panel.key(13,mock);CHECK(retries==1);
    panel.key(27,mock);CHECK(panel.visible&&!panel.reviewing);CHECK(panel.key(27,mock)==CreditReviewResult::Close&&!panel.visible&&!panel.reveal);
    for(auto state:{CreditOwnerState::Stopped,CreditOwnerState::Starting,CreditOwnerState::Ready,CreditOwnerState::Busy,CreditOwnerState::Faulted}){
        auto unavailable=std::make_shared<CreditOwnerSnapshot>(*s);unavailable->state=state;panel.open(unavailable);panel.key('r',mock);CHECK(!panel.reviewing);}
    for(auto state:{CreditRecoveryState::Disabled,CreditRecoveryState::Empty,CreditRecoveryState::Prepared,CreditRecoveryState::Confirmed,CreditRecoveryState::Rejected,CreditRecoveryState::Diverged,CreditRecoveryState::Unavailable}){
        auto stateful=std::make_shared<CreditOwnerSnapshot>(*s);stateful->recoveryState=state;panel.open(stateful);CHECK(panel.lines().size()>10);}
    auto volatileInput=std::make_shared<CreditOwnerSnapshot>(*s);volatileInput->persistentRecovery=false;panel.open(volatileInput);panel.key('r',mock);CHECK(!panel.reviewing&&has(panel,"volatile compatibility"));
    auto busy=std::make_shared<CreditOwnerSnapshot>(*s);busy->pendingJobs=1;panel.open(busy);panel.key('f',mock);panel.key('r',mock);CHECK(inspections==0&&!panel.reviewing);
    auto unsafe=std::make_shared<CreditOwnerSnapshot>(*s);unsafe->binding.scopeId="fixture.bad\x1b[2J\nlabel";panel.open(unsafe);panel.key('i');
    CHECK(panel.render(80,0,ColorLevel::None).find('\x1b')==std::string::npos&&has(panel,"fixture.bad?[2J?label"));
    auto unsupported=std::make_shared<CreditOwnerSnapshot>(*s);unsupported->binding.qualification=CreditQualification::Unsupported;
    panel.open(unsupported);panel.key('r',mock);CHECK(!panel.reviewing&&has(panel,"UNQUALIFIED"));
    panel.open(std::make_shared<const CreditOwnerSnapshot>());panel.key('i');CHECK(has(panel,"UNSUPPORTED asset")&&has(panel,"MISSING declared scale"));
    panel.open(s);panel.key('f',mock);CHECK(inspections==1);CHECK(panel.key('q',mock)==CreditReviewResult::Quit&&panel.key(3,mock)==CreditReviewResult::Quit);
    for(auto status:{CreditEnqueueStatus::NotReady,CreditEnqueueStatus::StaleGeneration,CreditEnqueueStatus::Full,CreditEnqueueStatus::Invalid,CreditEnqueueStatus::NeedsReconciliation,CreditEnqueueStatus::ReviewChanged}){
        auto failed=mock;failed.retry=[status](uint64_t,uint64_t){return CreditEnqueueResult{status,0};};panel.open(s);panel.key('r',failed);panel.key('i',failed);panel.key(' ',failed);panel.key(13,failed);CHECK(!panel.reviewing&&!panel.submittedTicket&&!panel.message.empty());}
    for(int width:{60,80,120})for(int height:{12,24}){panel.open(s);panel.key('r',mock);panel.key('i',mock);
        for(unsigned scroll=0;scroll<20;++scroll){const auto screen=panel.render(width,height,ColorLevel::None);CHECK(screen.find('\x1b')==std::string::npos&&std::count(screen.begin(),screen.end(),'\n')<=height);panel.key(kKeyPageDown,mock);}}
    SettingsPanel settings;settings.open({});settings.draft.color=false;settings.group=8;settings.field=2;settings.groupsFocused=false;
    CHECK(settings.fields()==6&&settings.key(13,{})==SettingsResult::None&&settings.credit.visible&&settings.dirty());
    CHECK(settings.render(80,24,ColorLevel::None).find("no qualified local credit collector")!=std::string::npos);settings.key(27,{});CHECK(!settings.credit.visible&&settings.dirty());
    const auto root=std::filesystem::path(makeScratchDir("credit-review"));
    {
        const auto file=root/"prepared.db";const auto original=prepared(file);const auto journal=std::filesystem::path(file.string()+".credit-recovery.json");const auto before=bytes(journal);
        CreditLedgerOwner owner(helper,file,contract(),{},[]{return T+1000;});CHECK(owner.start(1)&&ready(owner));const auto first=owner.snapshot();
        CHECK(first->persistentRecovery&&first->uncertainBatch&&first->uncertainBatch->records[0].amount.text()=="2.5"&&bytes(journal)==before);
        panel.open(first);auto live=access(owner);panel.key('r',live);panel.key('i',live);panel.key(' ',live);
        CHECK(owner.retryReviewed(2,first->revision).status==CreditEnqueueStatus::StaleGeneration);
        CHECK(owner.retryReviewed(1,0).status==CreditEnqueueStatus::ReviewChanged);
        const auto refresh=owner.refresh(1);CHECK(refresh.status==CreditEnqueueStatus::Queued&&until([&]{return owner.snapshot()->lastSuccessfulTicket==refresh.ticket;}));
        // Re-observation races with the final click: the owner must reject stale revision atomically, even without panel.update.
        panel.key(13,live);CHECK(!panel.submittedTicket&&panel.message.find("revision changed")!=std::string::npos&&bytes(journal)==before);
        panel.update(owner.snapshot());panel.key('r',live);panel.key(' ',live);panel.key(13,live);const auto ticket=panel.submittedTicket;CHECK(ticket>0);
        panel.key(13,live);CHECK(until([&]{return owner.snapshot()->lastSuccessfulTicket==ticket;}));panel.update(owner.snapshot());
        CHECK(owner.snapshot()->state==CreditOwnerState::Ready&&!owner.snapshot()->uncertainBatch&&owner.snapshot()->reply->applied==CreditLedgerStatus::Ok);
        CHECK(!panel.submittedTicket&&panel.message.find("confirmed: Ok with persisted receipt")!=std::string::npos);
        CHECK(first->uncertainBatch->snapshotId==original.batch->snapshotId&&first->uncertainBatch->observedAtMs==T&&first->writeOutcomeUnknown);
        CHECK(owner.retryReviewed(1,owner.snapshot()->revision).status==CreditEnqueueStatus::NotReady);
        CHECK(owner.snapshot()->reply->totals[0].knownPostedNet.text()=="2.5"&&owner.snapshot()->reply->totals[0].observedAtMs==T);
        owner.stop();panel.update(owner.snapshot());panel.key('r',live);CHECK(!panel.reviewing);
    }
    {
        CreditOwnerPolicy policy;policy.operationTimeoutMs=1000;const auto file=root/"lost-response.db";
        CreditLedgerOwner owner(fixture,file,contract(),policy,[]{return T;});CHECK(owner.start(1)&&ready(owner));
        CHECK(owner.submit(1,batch(1,T,{record("fixture.stall-after-apply",1,T,"3.75")})).status==CreditEnqueueStatus::Queued);
        CHECK(until([&]{return owner.snapshot()->state==CreditOwnerState::Faulted;}));const auto fault=owner.snapshot();
        CHECK(!fault->reply&&fault->writeOutcomeUnknown&&fault->uncertainBatch&&fault->uncertainBatch->records[0].amount.text()=="3.75");
        panel.open(fault);panel.key('i');CHECK(has(panel,"3.75 PRL")&&has(panel,"UNKNOWN WRITE OUTCOME"));panel.key('r',access(owner));CHECK(!panel.reviewing);owner.stop();
        CHECK(fault->uncertainBatch->records[0].postedAtMs==T);
    }
    {
        const auto file=root/"volatile.db";auto r=prepared(file);CreditOwnerPolicy policy;policy.persistentRecovery=false;
        CreditLedgerOwner owner(helper,file,contract(),policy,[]{return T;});CHECK(owner.start(1)&&ready(owner));
        CHECK(owner.retryReviewed(1,owner.snapshot()->revision).status==CreditEnqueueStatus::Invalid);owner.stop();
    }
    std::filesystem::remove_all(root);return finishTests("credit review");
}
