#include "product/game_guard.h"
#include "credit_fixture.h"
#include "product_test_util.h"
#include <functional>
using namespace om;
namespace {
using S=GameGuardState;using P=GameProfitStatus;
constexpr int64_t T=1793145600000LL;
ExactDecimal n(const std::string &s){ExactDecimal out;if(!ExactDecimal::parse(s,&out))std::abort();return out;}
ForecastRatio amount(const std::string &s,const std::string &den="1"){return {n(s),n(den)};}
GameProfitBinding binding(){return {"fixture.game","fixture.coexist-profile","fixture.episode-a",credit_fixture::contract(),{PowerBasis::GpuBoard,PowerOrigin::SyntheticFixture,"fixture.board","fixture.sensor"}};}
GameGuardTime at(uint64_t ms){return {T+static_cast<int64_t>(ms),1000+ms};}
GameDetection detect(uint64_t ms,GamePresence state=GamePresence::Present,const std::string &episode="fixture.episode-a"){
    static uint64_t revision=0;return {state,"fixture.game",state==GamePresence::Present?episode:"",1,++revision,at(ms)};
}
GameGuardPolicy policy(){GameGuardPolicy p;p.mode=GameIntent::Coexist;p.coexistence=ForecastQualification::SyntheticFixture;return p;}
GameSafety safe(){return {true,true,false};}
GameProfitInput input(uint64_t end=gameProfitWindowMs,uint64_t revision=1,const std::string &revenue="2",const std::string &cost="3",const std::string &episode="fixture.episode-a"){
    GameProfitInput p;p.qualification=ForecastQualification::SyntheticFixture;p.binding=binding();p.binding.episodeKey=episode;p.generation=1;p.revision=revision;
    p.beginMonoMs=1000+end-gameProfitWindowMs;p.endMonoMs=1000+end;p.beginUtcMs=T+static_cast<int64_t>(end-gameProfitWindowMs);p.endUtcMs=T+static_cast<int64_t>(end);
    p.intervals={{p.beginMonoMs,p.endMonoMs,"fixture.model-receipt",amount(revenue),amount(cost)}};return p;
}
std::shared_ptr<const GameProfitWindow> proof(uint64_t end=gameProfitWindowMs,uint64_t revision=1,const std::string &revenue="2",const std::string &cost="3",const std::string &episode="fixture.episode-a"){
    auto p=buildGameProfitWindow(input(end,revision,revenue,cost,episode));CHECK(static_cast<bool>(p));return p;
}
std::string text(const ForecastRatio &r){std::string s;CHECK(r.format(10,&s));return s;}
void reject(P expected,const std::function<void(GameProfitInput &)> &change){auto p=input();change(p);P status=P::Ok;CHECK(!buildGameProfitWindow(p,&status)&&status==expected);}
}
int main(){
    const auto negative=proof();CHECK(!negative->positive()&&text(negative->netUsd())=="-1.0000000000"&&text(negative->meanNetUsdPerSecond())=="-0.0016666667");
    {auto p=input();p.intervals={{1000,101000,"fixture.first",amount("2"),amount("1")},{101000,601000,"fixture.second",amount("0"),amount("2")}};
        const auto weighted=buildGameProfitWindow(p);CHECK(weighted&&!weighted->positive()&&text(weighted->netUsd())=="-1.0000000000");} // unweighted mean of the two rates would falsely be positive
    {auto p=input();p.intervals={{1000,301000,"fixture.first",amount("1","3"),amount("1","7")},{301000,601000,"fixture.second",amount("2","3"),amount("2","7")}};
        const auto fractional=buildGameProfitWindow(p);CHECK(fractional&&fractional->positive()&&text(fractional->netUsd())=="0.5714285714");}
    CHECK(!proof(600000,1,"0","0")->positive()&&proof(600000,1,"0","-1")->positive());
    for(auto change:std::vector<std::function<void(GameProfitInput &)>>{
        [](auto &p){p.qualification=ForecastQualification::Unsupported;},[](auto &p){p.binding.production.scope=CreditScope::Account;},
        [](auto &p){p.binding.power.origin=PowerOrigin::ObservedTelemetry;},[](auto &p){p.binding.power.basis=PowerBasis::DirectIncrementalWall;}})reject(P::Unsupported,change);
    for(auto change:std::vector<std::function<void(GameProfitInput &)>>{
        [](auto &p){p.generation=0;},[](auto &p){p.revision=0;},[](auto &p){p.binding.episodeKey="inferred-episode";},[](auto &p){p.binding.profileKey.clear();},
        [](auto &p){p.intervals[0].modelReceipt.clear();},[](auto &p){p.intervals[0].expectedRevenueUsd=amount("-1");},
        [](auto &p){p.intervals[0].estimatedCostUsd=amount("1","0");},[](auto &p){p.intervals[0].expectedRevenueUsd=amount("1","-1");},
        [](auto &p){p.intervals[0].estimatedCostUsd=amount("0.0000000000000000001");}})reject(P::Invalid,change);
    for(auto change:std::vector<std::function<void(GameProfitInput &)>>{
        [](auto &p){p.intervals.clear();},[](auto &p){p.intervals[0].beginMonoMs++;},[](auto &p){p.intervals[0].endMonoMs--;},
        [](auto &p){p.intervals[0].expectedRevenueUsd.reset();},[](auto &p){p.intervals[0].estimatedCostUsd.reset();},
        [](auto &p){p.endMonoMs--;p.endUtcMs--;}})reject(P::Coverage,change);
    reject(P::Clock,[](auto &p){p.endUtcMs++;});reject(P::Clock,[](auto &p){p.beginUtcMs=-1;});reject(P::Capacity,[](auto &p){p.intervals.resize(4097);});
    reject(P::Capacity,[](auto &p){p.intervals.clear();for(uint64_t i=0;i<3;++i)p.intervals.push_back({1000+i*200000,1000+(i+1)*200000,"fixture.rational",amount("1",std::string(37,'9')+std::to_string(9-i)),amount("0")});});
    {auto p=input();std::atomic<bool> cancel{true};P status;CHECK(!buildGameProfitWindow(p,&status,&cancel)&&status==P::Cancelled);}
    {auto p=input();p.intervals.clear();for(uint64_t i=0;i<4096;++i){const auto a=1000+600000*i/4096,b=1000+600000*(i+1)/4096;p.intervals.push_back({a,b,"fixture.segment",amount("1"),amount("0")});}
        const auto full=buildGameProfitWindow(p);CHECK(full&&text(full->netUsd())=="4096.0000000000");}
    SessionIntent intent;const auto token=intent.explicitStart();
    {GameSessionGuard off({},binding(),intent,token);CHECK(off.update(at(0),{},{}).state==S::Off);}
    {auto p=policy();p.mode=GameIntent::Pause;p.coexistence=ForecastQualification::Unsupported;GameSessionGuard pause(p,binding(),intent,token);
        CHECK(pause.update(at(0),detect(0),safe()).state==S::PausedPolicy&&!pause.snapshot().coexistBeginMonoMs);
        CHECK(pause.update(at(1000),detect(1000,GamePresence::Absent),safe()).state==S::PausedUnknown);
        CHECK(pause.update(at(3000),detect(3000,GamePresence::Absent),safe()).state==S::NormalEligible);}
    {auto p=policy();p.coexistence=ForecastQualification::Unsupported;GameSessionGuard q(p,binding(),intent,token);CHECK(q.update(at(0),detect(0),safe()).state==S::NeedsQualification);}
    for(auto change:std::vector<std::function<void(GameGuardPolicy &)>>{
        [](auto &p){p.mode=static_cast<GameIntent>(99);},[](auto &p){p.exitDebounceMs=30001;},[](auto &p){p.maximumDetectionGapMs=0;},
        [](auto &p){p.maximumDetectionGapMs=30001;},[](auto &p){p.maximumProfitAgeMs=0;},[](auto &p){p.maximumProfitAgeMs=60001;}}){auto p=policy();change(p);GameSessionGuard q(p,binding(),intent,token);CHECK(q.update(at(0),detect(0),safe()).state==S::Faulted);}
    // The first ten minutes are an explicitly qualified coexistence trial; exactly zero is not positive.
    for(const auto &bad:{negative,proof(600000,1,"3","3")}){GameSessionGuard q(policy(),binding(),intent,token);
        CHECK(q.update(at(0),detect(0),safe()).state==S::CoexistEligible&&q.snapshot().coexistBeginMonoMs==1000);
        CHECK(q.update(at(599999),detect(599999),safe(),bad).state==S::CoexistEligible&&!q.snapshot().profitCheckDue);
        CHECK(q.update(at(600000),detect(600000),safe(),bad).state==S::ProfitLocked&&q.snapshot().profitLocked);
        CHECK(q.update(at(600001),detect(600001),safe(),proof(600001,2,"100","0")).state==S::ProfitLocked&&q.snapshot().lastProfit==bad);
        CHECK(q.update(at(600002),detect(600002,GamePresence::Present,"fixture.replacement"),safe()).state==S::PausedUnknown&&q.snapshot().profitLocked);
        CHECK(q.update(at(600003),detect(600003),safe()).state==S::ProfitLocked);
        // Cached absence and unknown detection cannot unlock; fresh continuous absence is required.
        const auto gone=detect(600004,GamePresence::Absent);CHECK(q.update(at(600004),gone,safe()).state==S::PausedUnknown);
        CHECK(q.update(at(602004),gone,safe()).state==S::PausedUnknown&&q.snapshot().profitLocked);
        CHECK(q.update(at(602005),detect(602005,GamePresence::Unknown),safe()).state==S::PausedUnknown);
        CHECK(q.update(at(602006),detect(602006,GamePresence::Absent),safe()).state==S::PausedUnknown);
        CHECK(q.update(at(603006),detect(603006,GamePresence::Absent),safe()).state==S::PausedUnknown);
        CHECK(q.update(at(604006),detect(604006,GamePresence::Absent),safe()).state==S::NormalEligible&&!q.snapshot().profitLocked&&!q.snapshot().lastProfit);
        CHECK(q.update(at(604007),detect(604007,GamePresence::Present,"fixture.episode-b"),safe(),bad).state==S::CoexistEligible&&q.snapshot().coexistBeginMonoMs==605007);}
    // Missing/expired/inconsistent proof pauses as unavailable, never manufactures a zero or erases the episode timer.
    {GameSessionGuard q(policy(),binding(),intent,token);q.update(at(0),detect(0),safe());q.update(at(600000),detect(600000),safe(),negative);
        CHECK(q.update(at(600001),detect(600001,GamePresence::Absent),safe()).state==S::PausedUnknown);
        CHECK(q.update(at(603001),detect(603001,GamePresence::Absent),safe()).state==S::PausedUnknown&&q.snapshot().profitLocked);
        CHECK(q.update(at(605001),detect(605001,GamePresence::Absent),safe()).state==S::NormalEligible);}
    {GameSessionGuard q(policy(),binding(),intent,token);q.update(at(0),detect(0),safe());CHECK(q.update(at(600000),detect(600000),safe()).state==S::ProfitUnavailable&&!q.snapshot().profitLocked);
        const auto good=proof(600001,1,"4","1");CHECK(q.update(at(600001),detect(600001),safe(),good).state==S::CoexistEligible);
        CHECK(q.update(at(610001),detect(610001),safe()).state==S::CoexistEligible);
        CHECK(q.update(at(610002),detect(610002),safe()).state==S::ProfitUnavailable&&q.snapshot().lastProfit==good);
        CHECK(q.update(at(610003),detect(610003),safe(),proof(610003,2)).state==S::ProfitLocked);} // rolling complete ten-minute evidence after initial probation
    for(unsigned bad=0;bad<6;++bad){GameSessionGuard q(policy(),binding(),intent,token);q.update(at(0),detect(0),safe());auto p=input();
        if(bad==0)p.binding.episodeKey="fixture.other";
        if(bad==1)p.binding.production.scopeId+="-other";
        if(bad==2)p.binding.profileKey="fixture.other";
        if(bad==3)p.generation++;
        if(bad==4){p.beginUtcMs++;p.endUtcMs++;}
        if(bad==5)p.binding.power.adapter="fixture.other";
        const auto invalid=buildGameProfitWindow(p);CHECK(invalid&&q.update(at(600001),detect(600001),safe(),invalid).state==S::ProfitUnavailable&&!q.snapshot().profitLocked);}
    {GameSessionGuard q(policy(),binding(),intent,token);q.update(at(0),detect(0),safe());const auto good=proof(600000,2,"4","1");
        CHECK(q.update(at(600000),detect(600000),safe(),good).state==S::CoexistEligible);
        CHECK(q.update(at(600001),detect(600001),safe(),proof(600001,1)).state==S::ProfitUnavailable&&!q.snapshot().profitLocked);
        CHECK(q.update(at(600002),detect(600002),safe(),buildGameProfitWindow(good->input())).state==S::ProfitUnavailable);
        CHECK(q.update(at(600003),detect(600003),safe(),good).state==S::CoexistEligible);}
    // Frozen detection identity/timestamps prevent stale exit notices, even if later delivered at a fresh poll time.
    {GameSessionGuard q(policy(),binding(),intent,token);const auto initial=detect(0);q.update(at(0),initial,safe());q.update(at(600000),detect(600000),safe(),negative);
        auto stale=initial;stale.state=GamePresence::Absent;stale.episodeKey.clear();CHECK(q.update(at(600001),stale,safe()).state==S::PausedUnknown&&q.snapshot().profitLocked);
        const auto current=detect(600002);CHECK(q.update(at(600002),current,safe()).state==S::ProfitLocked);auto forged=current;forged.state=GamePresence::Absent;forged.episodeKey.clear();
        CHECK(q.update(at(600003),forged,safe()).state==S::PausedUnknown&&q.snapshot().profitLocked);}
    // Thermal/sensor pauses keep the original real-time ten-minute window; a fault and user stop are terminal.
    {GameSessionGuard q(policy(),binding(),intent,token);auto unsafe=safe();unsafe.thermalSafe=false;
        CHECK(q.update(at(0),detect(0),unsafe).state==S::Cooling&&!q.snapshot().coexistBeginMonoMs);q.update(at(1000),detect(1000),safe());
        CHECK(q.update(at(5000),detect(5000),unsafe).state==S::Cooling&&q.snapshot().coexistBeginMonoMs==2000);
        CHECK(q.update(at(601000),detect(601000),safe(),proof(601000,1)).state==S::ProfitLocked);unsafe=safe();unsafe.fault=true;
        CHECK(q.update(at(601001),detect(601001),unsafe).state==S::Faulted&&q.update(at(601002),detect(601002),safe()).state==S::Faulted);}
    for(unsigned bad=0;bad<3;++bad){GameSessionGuard q(policy(),binding(),intent,token);q.update(at(0),detect(0),safe());auto now=at(1);
        if(bad==0)now.utcMs=T-1;
        if(bad==1)now.monotonicMs=999;
        if(bad==2)now.utcMs+=2001;
        CHECK(q.update(now,detect(1),safe()).state==S::ClockFault);}
    {GameSessionGuard q(policy(),binding(),intent,token);q.requestStop();CHECK(q.update(at(0),detect(0),safe()).state==S::Stopped);}
    {GameSessionGuard q(policy(),binding(),intent,token);q.update(at(0),detect(0),safe());q.update(at(600000),detect(600000),safe(),negative);intent.stop();
        CHECK(q.update(at(600001),detect(600001,GamePresence::Absent),safe()).state==S::Stopped);intent.explicitStart();
        CHECK(q.update(at(603001),detect(603001,GamePresence::Absent),safe()).state==S::Stopped&&q.snapshot().profitLocked);}
    return finishTests("game guard and ten-minute profit");
}
