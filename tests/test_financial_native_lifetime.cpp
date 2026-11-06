// Actual readonly/current-proof boundaries over private synthetic stores. No real tariff, sensor, credit or provider.
#include "product/native_process_admission.h"
#include "product/billing_compute_owner.h"
#include "product/forecast_owner.h"
#include "product/credit_recovery.h"
#include "billing_source_fixture.h"
#include "credit_fixture.h"
#include "product_test_util.h"
#include <chrono>
#include <fstream>
#include <thread>
namespace {
using namespace om;
thread_local bool failAllocations=false;std::atomic<unsigned> rejectedAllocations{0};
template<class P>bool until(P p){const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(6);
    while(!p()&&std::chrono::steady_clock::now()<end)std::this_thread::sleep_for(std::chrono::milliseconds(2));
    return p();}
void fault(){NativeProcessAdmission other;CHECK(other.begin());other.markUnresolved();other.settled();}
std::string bytes(const std::filesystem::path &p){std::ifstream in(p,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
}
bool canaryNativeAllocationFault(){if(!failAllocations)return false;++rejectedAllocations;return true;}
int main(int argc,char **argv){
    if(argc!=9&&argc!=10)return 4;
    const std::string kind=argv[1];if(kind!="billing"&&kind!="compute"&&kind!="forecast")return 4;
    const std::string action=argc==10?argv[9]:"revoked";
    const bool claimClock=action=="clock",allocation=action=="allocation",validation=action=="validation-allocation",foreign=action=="foreign",stopped=action=="stopped";
    if((action!="revoked"&&!claimClock&&!allocation&&!validation&&!foreign&&!stopped)||(claimClock&&kind!="compute")||(validation&&kind!="forecast"))return 4;
    const auto oldEpoch=nativeProcessEpoch();const auto caller=std::this_thread::get_id();std::atomic<bool> armClock{false};
    const auto clockFault=[&]{if(std::this_thread::get_id()==caller&&armClock.exchange(false)){failAllocations=allocation;fault();}};
    if(foreign)fault();
    BillingOwnerHelpers helpers{std::filesystem::absolute(argv[2]),std::filesystem::absolute(argv[3]),std::filesystem::absolute(argv[4]),std::filesystem::absolute(argv[5])};
    const auto trace=std::filesystem::absolute(argv[6]),credit=std::filesystem::absolute(argv[7]),forecast=std::filesystem::absolute(argv[8]);
    const auto root=std::filesystem::path(makeScratchDir("financial-native-originals")),file=root/"fixture-bill.db";
    if(kind=="billing"){
        BillingOwnerBinding context{file,billing_fixture::binding(),{}};
        BillingOwner owner(helpers,context,{true,5000},[&]{clockFault();return BillingOwnerTime{billing_fixture::T+2*electricity_fixture::hour,10000};});CHECK(owner.start(7));
        CHECK(until([&]{const auto s=owner.snapshot();return !s->pending&&s->completedTicket==1;}));const auto before=owner.snapshot();
        CHECK(before->state==BillingOwnerState::NeedsReview&&before->reviewable&&before->view&&before->view->stable&&before->view->stage&&!before->view->stage->present&&before->view->monetary->status==BillingStatus::Storage);
        CHECK(!std::filesystem::exists(file));
        if(foreign){{NativeProcessScope old(oldEpoch);CHECK(!owner.snapshot()->reviewable);CHECK(owner.createReviewed(7,before->revision)==BillingOwnerAdmission::Stopped);}CHECK(owner.snapshot()->reviewable);}
        if(stopped)owner.requestStop();
        if(allocation){armClock=true;CHECK(owner.refresh(7,before->revision)==BillingOwnerAdmission::Stopped);CHECK(!armClock);}
        else fault();
        const auto revoked=owner.snapshot();failAllocations=false;
        CHECK(allocation?!revoked->reviewable:revoked->state==(stopped?BillingOwnerState::Stopped:BillingOwnerState::Faulted)&&!revoked->reviewable&&revoked->transport==LocalTaskStatus::Unreaped);
        CHECK(owner.createReviewed(7,revoked->revision)!=BillingOwnerAdmission::Queued);CHECK(owner.refresh(7,revoked->revision)!=BillingOwnerAdmission::Queued);
        CHECK(before->reviewable&&before->view->stable);owner.stop();CHECK(!std::filesystem::exists(file));
    }else if(kind=="compute"){
        auto context=std::make_shared<BillingOwnerBinding>();context->file=file;context->binding=billing_source_fixture::binding(root/"fixture-source.db");
        const auto &binding=context->binding;const auto written=executeEnergyHistoryRequest(billing_source_fixture::input(binding,1));CHECK(written.applied==EnergyHistoryStatus::Ok);
        {BillingRecoveryStore store;CHECK(store.open(file,binding,billing_source_fixture::now,BillingRecoveryOpen::CreateNew)==BillingStatus::Ok);}
        auto money=std::make_shared<BillingOwnerSnapshot>();money->generation=7;money->revision=1;money->context=context;money->state=BillingOwnerState::Ready;money->reviewable=true;
        auto view=std::make_shared<BillingOwnerView>();view->stable=true;auto stage=std::make_shared<BillingInputSnapshot>();stage->status=BillingStatus::Ok;view->stage=stage;
        {BillingRecoveryStore store;CHECK(store.open(file,binding,billing_source_fixture::now,BillingRecoveryOpen::QueryOnly)==BillingStatus::Ok);view->monetary=std::make_shared<const BillingRecoverySnapshot>(store.inspect(billing_source_fixture::now));}
        money->view=view;const auto sourceBefore=bytes(binding.source->file),billBefore=bytes(file);
        BillingComputeOwner owner(trace,context,{true,5000,512},[&]{clockFault();return BillingOwnerTime{billing_source_fixture::now,10000};});CHECK(owner.start(7));CHECK(owner.observe(7,money));
        BillingSourceNotice notice{{written.checkpoint.sequence,written.checkpoint.digest},written.checkpoint.throughUtcMs,written.checkpoint.observedAtMs};
        CHECK(owner.notify(7,owner.snapshot()->revision,notice)==BillingComputeAdmission::Queued);CHECK(until([&]{return !owner.snapshot()->pending;}));const auto before=owner.snapshot();
        CHECK(before->state==BillingComputeState::Ready&&before->reviewable&&before->candidate&&before->candidate->original.receipt);
        if(foreign){{NativeProcessScope old(oldEpoch);CHECK(!owner.snapshot()->reviewable);CHECK(!owner.claimReviewed(7,before->revision,money->generation,money->revision));}CHECK(owner.snapshot()->reviewable);}
        if(stopped)owner.requestStop();
        if(claimClock||allocation){armClock=true;CHECK(!owner.claimReviewed(7,before->revision,money->generation,money->revision));CHECK(!armClock);}
        else fault();
        const auto revoked=owner.snapshot();failAllocations=false;
        CHECK(allocation?!revoked->reviewable:revoked->state==(stopped?BillingComputeState::Stopped:BillingComputeState::Faulted)&&!revoked->reviewable&&revoked->transport==LocalTaskStatus::Unreaped);
        CHECK(!owner.claimReviewed(7,revoked->revision,money->generation,money->revision));CHECK(!owner.observe(7,money));CHECK(owner.notify(7,revoked->revision,notice)!=BillingComputeAdmission::Queued);
        CHECK(before->reviewable&&before->candidate->original.receipt);owner.stop();CHECK(bytes(file)==billBefore&&bytes(binding.source->file)==sourceBefore);
    }else{
        constexpr int64_t now=credit_fixture::T,day=86400000;auto binding=credit_fixture::contract();binding.historyStartMs=now/day*day-2*day;
        binding.cursor=CreditCursorContract{"fixture.exclusive-acyclic-v1","fixture:initial"};CreditProcessRequest request;request.generation=request.ticket=1;request.file=root/"fixture-credit.db";
        request.binding=binding;request.nowUtcMs=now;request.recovery=CreditRecoveryMode::Submit;request.queries={{true,0,now}};
        CreditBatch batch;batch.binding=binding;batch.sequence=1;batch.snapshotId="fixture.native-original";batch.sourceAsOfMs=batch.observedAtMs=now;
        batch.coverage={{binding.historyStartMs-1,now}};batch.records={credit_fixture::record("fixture.posted-original",1,now,"2")};batch.cursor=CreditCursorStep{binding.cursor->initial,"fixture:page-1"};request.batch=batch;
        CreditProcessClient writer(credit);std::atomic<bool> cancel{false};const auto posted=writer.transact(request,cancel,5000);CHECK(posted.reply&&posted.reply->applied==CreditLedgerStatus::Ok&&!posted.writeOutcomeUnknown);
        const auto beforeDb=bytes(request.file),beforeJournal=bytes(request.file.string()+".credit-recovery.json");ForecastOwnerPolicy policy;policy.enabled=true;
        IncomeForecastOwner owner(forecast,request.file,binding,policy,[&]{clockFault();return ForecastTime{now,10000};});CHECK(owner.start(7));ForecastScenario scenario;
        scenario.reportUtcMs=now;scenario.qualification=ForecastQualification::SyntheticFixture;scenario.remaining.work.binding=binding;
        scenario.remaining.work.qualification=ForecastQualification::SyntheticFixture;scenario.remaining.work.profileKey="fixture.profile";
        scenario.remaining.assumptionKey="fixture.no-future-estimate";scenario.remaining.declaredUtcMs=now;
        CHECK(owner.submit(7,scenario)==ForecastAdmission::Queued);CHECK(until([&]{return !owner.snapshot()->pending;}));const auto before=owner.snapshot();
        CHECK(before->state==ForecastOwnerState::Ready&&before->result&&before->result->todayActual.complete&&before->result->todayActual.knownTokens);
        if(validation){failAllocations=true;CHECK(owner.submit(7,scenario)==ForecastAdmission::Invalid);failAllocations=false;CHECK(owner.snapshot()->result==before->result);}
        if(foreign){{NativeProcessScope old(oldEpoch);CHECK(!owner.snapshot()->result);CHECK(owner.submit(7,scenario)==ForecastAdmission::Stopped);}CHECK(owner.snapshot()->result==before->result);}
        if(stopped)owner.requestStop();
        if(allocation){armClock=true;CHECK(owner.submit(7,scenario)==ForecastAdmission::Stopped);CHECK(!armClock);}
        else fault();
        const auto revoked=owner.snapshot();failAllocations=false;
        CHECK(allocation?!revoked->result:revoked->state==(stopped?ForecastOwnerState::Stopped:ForecastOwnerState::Faulted)&&!revoked->result&&revoked->transport==LocalTaskStatus::Unreaped);
        CHECK(owner.submit(7,scenario)==ForecastAdmission::Stopped);CHECK(owner.updateMarket(7,{},{})==ForecastAdmission::Stopped);CHECK(!owner.invalidate(7));
        CHECK(before->result&&before->result->todayActual.knownTokens);owner.stop();CHECK(bytes(request.file)==beforeDb&&bytes(request.file.string()+".credit-recovery.json")==beforeJournal);
    }
    CHECK(!(allocation||validation)||rejectedAllocations>0);CHECK(!nativeProcessUnresolved());std::filesystem::remove_all(root);
    return finishTests("actual synthetic financial originals versus revoked native lifetime authority");
}
