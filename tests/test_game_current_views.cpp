// Synthetic game episodes/financial evidence and an Off runtime only. No detector/helper, game, GPU, network or mining.
#include "product/game_runtime.h"
#include "credit_fixture.h"
#include "product_test_util.h"
namespace {
using namespace om;using S=GameGuardState;
std::atomic<bool> rejectAllocations{false};std::atomic<unsigned> allocationAttempts{0};
constexpr int64_t T=1793145600000LL;
GameGuardTime at(uint64_t ms=0){return {T+static_cast<int64_t>(ms),1000000+ms};}
GameSafety safe(){return {true,true,false};}
void fault(){NativeProcessAdmission other;CHECK(other.begin());other.markUnresolved();other.settled();}
GameProfitBinding binding(){return {"fixture.game","fixture.coexist-profile","fixture.episode",credit_fixture::contract(),
    {PowerBasis::GpuBoard,PowerOrigin::SyntheticFixture,"fixture.board","fixture.sensor"}};}
GameDetection detection(SessionToken token,uint64_t ms,uint64_t revision){return {GamePresence::Present,"fixture.game","fixture.episode",token.epoch,revision,at(ms)};}
ForecastRatio amount(const char *text){ExactDecimal value,one;CHECK(ExactDecimal::parse(text,&value)&&ExactDecimal::parse("1",&one));return {value,one};}
std::shared_ptr<const GameProfitWindow> loss(SessionToken token){GameProfitInput p;p.qualification=ForecastQualification::SyntheticFixture;p.binding=binding();
    p.generation=token.epoch;p.revision=1;p.beginUtcMs=at().utcMs;p.endUtcMs=at(gameProfitWindowMs).utcMs;p.beginMonoMs=at().monotonicMs;p.endMonoMs=at(gameProfitWindowMs).monotonicMs;
    p.intervals={{p.beginMonoMs,p.endMonoMs,"fixture.original-model",amount("2"),amount("3")}};return buildGameProfitWindow(p);}
void guard(const std::string &kind,const std::string &action,uint64_t foreignEpoch){
    SessionIntent intent;const auto token=intent.explicitStart();GameGuardPolicy p;p.mode=kind=="force"?GameIntent::Pause:GameIntent::Coexist;
    p.coexistence=ForecastQualification::SyntheticFixture;GameSessionGuard owner(p,binding(),intent,token);
    std::shared_ptr<const GameProfitWindow> proof;uint64_t now=1;
    if(kind=="force"){
        CHECK(owner.update(at(),detection(token,0,1),safe()).state==S::PausedPolicy);CHECK(owner.requestForceResume());
        CHECK(owner.update(at(1),detection(token,1,2),safe()).state==S::ForceResumeEligible&&owner.forceResumeActive());
    }else{
        proof=loss(token);CHECK(proof&&!proof->positive());CHECK(owner.update(at(),detection(token,0,1),safe()).state==S::CoexistEligible);
        now=gameProfitWindowMs;CHECK(owner.update(at(now),detection(token,now,2),safe(),proof).state==S::ProfitLocked);
    }
    const auto before=owner.snapshot();
    if(action=="clock-fault"){auto skewed=at(now+1);skewed.utcMs+=2001;
        CHECK(owner.update(skewed,detection(token,now+1,3),safe()).state==S::ClockFault);
        CHECK(!owner.forceResumeActive()&&!owner.snapshot().forceResumeActive&&!owner.requestForceResume());
        CHECK(owner.snapshot().episodeKey==before.episodeKey);return;}
    if(action=="foreign"){{NativeProcessScope old(foreignEpoch);CHECK(owner.snapshot().state==S::Stopped&&!owner.snapshot().forceResumeActive&&!owner.snapshot().lastProfit);
        CHECK(!owner.forceResumeActive()&&!owner.requestForceResume());owner.cancelForceResume();
        CHECK(owner.update(at(now+900000),detection(token,now+900000,3),safe()).state==S::Stopped);}
        CHECK(owner.snapshot().state==before.state&&owner.snapshot().episodeKey==before.episodeKey&&owner.forceResumeActive()==before.forceResumeActive&&owner.snapshot().lastProfit==proof);
        CHECK(owner.update(at(now+1),detection(token,now+1,3),safe()).state==before.state);}
    if(action=="intent-stop")intent.stop();else fault();
    if(action=="allocation")rejectAllocations=true;
    const bool revoked=owner.snapshot().state==S::Stopped&&!owner.snapshot().forceResumeActive&&!owner.forceResumeActive();
    rejectAllocations=false;CHECK(revoked);if(action=="allocation")CHECK(allocationAttempts.load()==0);CHECK(!owner.requestForceResume());
    CHECK(owner.snapshot().episodeKey==before.episodeKey&&owner.snapshot().profitLocked==before.profitLocked&&owner.snapshot().lastProfit==proof);
    CHECK(before.state==(kind=="force"?S::ForceResumeEligible:S::ProfitLocked)&&before.episodeKey=="fixture.episode");
}
void runtime(const std::string &action,uint64_t foreignEpoch){
    SessionIntent intent;const auto token=intent.explicitStart();GameRuntime owner("fixture-unused-helper",{},{},{},{},intent,token);
    CHECK(owner.start(at())&&owner.state()==GameRuntimeState::Off);CHECK(owner.tick(at(1),safe()).state==S::Off);
    CHECK(owner.runDeadline(at(1),at(100).monotonicMs)==at(100).monotonicMs&&owner.detection()->completed==0);
    const auto before=owner.snapshot();
    if(action=="foreign"){{NativeProcessScope old(foreignEpoch);CHECK(owner.state()==GameRuntimeState::Stopped&&owner.snapshot().state==S::Stopped);
        CHECK(owner.tick(at(900000),safe()).state==S::Stopped);CHECK(!owner.requestForceResume()&&owner.runDeadline(at(1),at(100).monotonicMs)==0);}
        CHECK(owner.state()==GameRuntimeState::Off&&owner.snapshot().state==S::Off);CHECK(owner.tick(at(2),safe()).state==S::Off&&owner.runDeadline(at(2),at(100).monotonicMs)==at(100).monotonicMs);}
    if(action=="intent-stop")intent.stop();else fault();
    CHECK(owner.state()==GameRuntimeState::Stopped&&owner.snapshot().state==S::Stopped&&!owner.forceResumeActive());
    CHECK(owner.runDeadline(at(1),at(100).monotonicMs)==0&&owner.detection()->state==GameDetectionOwnerState::Stopped&&owner.detection()->completed==0);
    CHECK(before.state==S::Off);owner.stop();CHECK(!owner.start(at(3)));
}
}
bool canaryNativeAllocationFault(){if(!rejectAllocations.load())return false;++allocationAttempts;return true;}
int main(int argc,char **argv){
    if(argc!=3)return 4;
    const std::string kind=argv[1],action=argv[2];
    if((kind!="force"&&kind!="profit"&&kind!="runtime")||(action!="fault"&&action!="intent-stop"&&action!="foreign"&&action!="clock-fault"&&action!="allocation")||(action=="clock-fault"&&kind!="force")||(action=="allocation"&&kind=="runtime"))return 4;
    const auto original=nativeProcessEpoch();if(action=="foreign")fault();
    if(kind=="runtime")runtime(action,original);else guard(kind,action,original);
    CHECK(!nativeProcessUnresolved());return finishTests("current synthetic game state versus original parent/caller");
}
