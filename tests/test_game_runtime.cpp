#include "product/game_runtime.h"
#include "credit_fixture.h"
#include "product_test_util.h"
#include <thread>
using namespace om;
namespace {
using S=GameGuardState;using R=GameRuntimeState;
bool wait(const std::function<bool()> &done,unsigned ms=5000){const auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(ms);
    do{if(done())return true;std::this_thread::sleep_for(std::chrono::milliseconds(5));}while(std::chrono::steady_clock::now()<end);return done();}
GameGuardPolicy policy(){GameGuardPolicy p;p.mode=GameIntent::Pause;p.exitDebounceMs=100;return p;}
GameDetectionPolicy detection(){GameDetectionPolicy p;p.enabled=true;p.pollMs=100;return p;}
GameProcessTarget target(const char *name="fixture-present"){return {"fixture.game",name,GameProcessAdapter::WindowsSessionImage};}
GameProfitBinding binding(){GameProfitBinding b;b.gameKey="fixture.game";return b;}
GameSafety safe(){return {true,true,false};}
}
int main(int argc,char **argv){
    if(argc!=2)return 2;
    const auto helper=std::filesystem::absolute(argv[1]);SessionIntent intent;const auto token=intent.explicitStart();
    {GameRuntime r(helper,target(),detection(),policy(),binding(),intent,token);CHECK(r.state()==R::Dormant&&r.snapshot().state==S::PausedUnknown);
        CHECK(r.tick(gameDetectionTime(),safe()).state==S::PausedUnknown&&r.detection()->completed==0);
        CHECK(r.start(gameDetectionTime())&&!r.start(gameDetectionTime()));
        CHECK(wait([&]{return r.tick(gameDetectionTime(),safe()).state==S::PausedPolicy;}));
        const auto first=r.detection();CHECK(first->detection.generation==token.epoch&&first->detection.revision&&r.snapshot().episodeKey==first->detection.episodeKey);
        auto unsafe=safe();unsafe.thermalSafe=false;CHECK(r.tick(gameDetectionTime(),unsafe).state==S::Cooling);
        CHECK(r.tick(gameDetectionTime(),safe()).state==S::PausedPolicy&&r.snapshot().episodeKey==first->detection.episodeKey);
        unsafe=safe();unsafe.fault=true;CHECK(r.tick(gameDetectionTime(),unsafe).state==S::Faulted&&r.state()==R::Faulted);
        CHECK(r.tick(gameDetectionTime(),safe()).state==S::Faulted&&!r.start(gameDetectionTime()));r.stop();CHECK(r.snapshot().state==S::Stopped);}
    {GameRuntime r(helper,target("fixture-sequence"),detection(),policy(),binding(),intent,token);CHECK(r.start(gameDetectionTime()));
        CHECK(wait([&]{return r.tick(gameDetectionTime(),safe()).state==S::PausedPolicy;}));
        const auto episode=r.snapshot().episodeKey;
        CHECK(wait([&]{r.tick(gameDetectionTime(),safe());return r.detection()->detection.state==GamePresence::Absent;}));
        CHECK(r.snapshot().state==S::PausedUnknown&&r.snapshot().episodeKey==episode);
        const auto observed=r.detection()->detection;const auto again=r.tick(gameDetectionTime(),safe());
        CHECK(again.state==S::PausedUnknown&&r.detection()->detection.observed.monotonicMs==observed.observed.monotonicMs);
        CHECK(wait([&]{return r.tick(gameDetectionTime(),safe()).state==S::NormalEligible;}));
        CHECK(r.snapshot().episodeKey.empty()&&r.state()==R::Active);r.stop();}
    {auto off=policy();off.mode=GameIntent::Off;GameRuntime r("missing",{},detection(),off,{},intent,token);CHECK(r.start(gameDetectionTime())&&r.state()==R::Off);
        CHECK(r.tick(gameDetectionTime(),safe()).state==S::Off&&r.detection()->completed==0);
        CHECK(r.tick(gameDetectionTime(),{false,false,true}).state==S::Faulted&&r.state()==R::Faulted);r.stop();}
    for(const auto mode:{GameIntent::Off,GameIntent::Pause}){
        auto p=policy();p.mode=mode;GameRuntime r(helper,target(),detection(),p,binding(),intent,token);auto bad=gameDetectionTime();bad.utcMs=-1;
        CHECK(!r.start(bad)&&r.state()==R::Faulted&&r.snapshot().state==S::ClockFault);
        CHECK(r.detection()->state==GameDetectionOwnerState::Stopped&&r.detection()->completed==0&&!r.detection()->pending);
        CHECK(!r.start(gameDetectionTime())&&r.tick(gameDetectionTime(),safe()).state==S::ClockFault);}
    {GameRuntime r(helper,target("fixture-incomplete"),detection(),policy(),binding(),intent,token);CHECK(r.start(gameDetectionTime()));
        CHECK(wait([&]{r.tick(gameDetectionTime(),safe());return r.detection()->completed>=1;}));CHECK(r.snapshot().state==S::PausedUnknown&&r.state()==R::Active);r.stop();}
    {auto co=policy();co.mode=GameIntent::Coexist;GameRuntime r(helper,target(),detection(),co,binding(),intent,token);CHECK(r.start(gameDetectionTime()));
        CHECK(wait([&]{return r.tick(gameDetectionTime(),safe()).state==S::NeedsQualification;}));CHECK(!r.snapshot().coexistBeginMonoMs);r.stop();}
    {auto co=policy();co.mode=GameIntent::Coexist;co.coexistence=ForecastQualification::SyntheticFixture;
        auto b=binding();b.profileKey="fixture.coexist-profile";b.production=credit_fixture::contract();b.power={PowerBasis::GpuBoard,PowerOrigin::SyntheticFixture,"fixture.board","fixture.sensor"};
        GameRuntime r(helper,target(),detection(),co,b,intent,token);CHECK(r.start(gameDetectionTime()));
        CHECK(wait([&]{return r.tick(gameDetectionTime(),safe()).state==S::NeedsQualification;}));
        CHECK(r.snapshot().episodeKey.rfind("process.",0)==0&&!r.snapshot().coexistBeginMonoMs);r.stop();} // native identity never becomes a synthetic coexistence permit
    {auto t=target();auto d=detection();auto p=policy();auto b=binding();GameRuntime r(helper,t,d,p,b,intent,token);
        t.executable="fixture-absent";t.gameKey="fixture.other";d.enabled=false;p.mode=GameIntent::Off;b.gameKey="fixture.other";
        CHECK(r.start(gameDetectionTime())&&wait([&]{return r.tick(gameDetectionTime(),safe()).state==S::PausedPolicy;}));
        CHECK(r.detection()->detection.gameKey=="fixture.game"&&r.snapshot().episodeKey.rfind("process.",0)==0);r.stop();}
    {auto d=detection();d.pollMs=3000;auto p=policy();p.maximumDetectionGapMs=500;GameRuntime r(helper,target(),d,p,binding(),intent,token);
        CHECK(r.start(gameDetectionTime())&&wait([&]{return r.tick(gameDetectionTime(),safe()).state==S::PausedPolicy;}));const auto first=r.detection();
        CHECK(wait([&]{return gameDetectionTime().monotonicMs-first->detection.observed.monotonicMs>p.maximumDetectionGapMs;}));
        CHECK(r.tick(gameDetectionTime(),safe()).state==S::PausedUnknown&&r.snapshot().episodeKey==first->detection.episodeKey);
        CHECK(r.detection()->detection.revision==first->detection.revision&&r.detection()->detection.observed.monotonicMs==first->detection.observed.monotonicMs);
        r.stop();CHECK(first->state==GameDetectionOwnerState::Ready&&r.detection()->state==GameDetectionOwnerState::Stopped);}
    {GameRuntime r(helper.parent_path()/"fixture-game-runtime-helper-missing",target(),detection(),policy(),binding(),intent,token);
        CHECK(r.start(gameDetectionTime())&&wait([&]{return r.detection()->completed>=1;}));
        CHECK(r.tick(gameDetectionTime(),safe()).state==S::PausedUnknown&&r.state()==R::Active);r.stop();}
    {auto p=policy();p.mode=static_cast<GameIntent>(99);GameRuntime r(helper,target(),detection(),p,binding(),intent,token);
        CHECK(!r.start(gameDetectionTime())&&r.snapshot().state==S::Faulted&&r.detection()->completed==0);}
    {auto t=target();t.executable="../fixture-present";GameRuntime r(helper,t,detection(),policy(),binding(),intent,token);
        CHECK(!r.start(gameDetectionTime())&&r.snapshot().state==S::Faulted&&r.detection()->completed==0);}
    {auto d=detection();d.pollMs=99;GameRuntime r(helper,target(),d,policy(),binding(),intent,token);
        CHECK(!r.start(gameDetectionTime())&&r.snapshot().state==S::Faulted&&r.detection()->completed==0);}
    {auto disabled=detection();disabled.enabled=false;GameRuntime r(helper,target(),disabled,policy(),binding(),intent,token);
        CHECK(!r.start(gameDetectionTime())&&r.state()==R::Faulted&&r.snapshot().state==S::Faulted&&r.detection()->completed==0);}
    {auto wrong=binding();wrong.gameKey="fixture.other";GameRuntime r(helper,target(),detection(),policy(),wrong,intent,token);
        CHECK(!r.start(gameDetectionTime())&&r.snapshot().state==S::Faulted&&r.detection()->completed==0);}
    {GameRuntime r(helper,target("fixture-clock"),detection(),policy(),binding(),intent,token);CHECK(r.start(gameDetectionTime()));
        CHECK(wait([&]{return r.detection()->state==GameDetectionOwnerState::ClockFault;}));
        CHECK(r.tick(gameDetectionTime(),safe()).state==S::Faulted&&r.state()==R::Faulted);
        CHECK(r.detection()->state==GameDetectionOwnerState::Stopped&&!r.start(gameDetectionTime()));r.stop();}
    {GameRuntime r(helper,target(),detection(),policy(),binding(),intent,token);const auto now=gameDetectionTime();CHECK(r.start(now));
        auto backwards=now;backwards.utcMs--;CHECK(r.tick(backwards,safe()).state==S::ClockFault&&r.state()==R::Faulted);CHECK(!r.start(gameDetectionTime()));r.stop();}
    {GameRuntime r(helper,target(),detection(),policy(),binding(),intent,token);r.requestStop();CHECK(!r.start(gameDetectionTime())&&r.snapshot().state==S::Stopped);}
    {GameRuntime r(helper,target(),detection(),policy(),binding(),intent,{});CHECK(!r.start(gameDetectionTime())&&r.snapshot().state==S::Stopped);}
    {GameRuntime r(helper,target("fixture-hang"),detection(),policy(),binding(),intent,token);CHECK(r.start(gameDetectionTime()));
        CHECK(wait([&]{r.tick(gameDetectionTime(),safe());return r.detection()->pending;}));
        r.requestStop();const auto revoked=r.detection();CHECK(r.snapshot().state==S::Stopped&&revoked->state==GameDetectionOwnerState::Stopped&&!revoked->pending);
        const auto began=gameDetectionTime();r.stop();CHECK(gameDetectionTime().monotonicMs-began.monotonicMs<1500);
        CHECK(r.detection()->state==GameDetectionOwnerState::Stopped&&r.detection()->completed==revoked->completed&&!r.start(gameDetectionTime()));}
    {GameRuntime r(helper,target(),detection(),policy(),binding(),intent,token);CHECK(r.start(gameDetectionTime()));
        CHECK(wait([&]{return r.tick(gameDetectionTime(),safe()).state==S::PausedPolicy;}));const auto episode=r.snapshot().episodeKey;intent.stop();
        auto bad=gameDetectionTime();bad.utcMs=-1;CHECK(r.tick(bad,{false,false,true}).state==S::Stopped&&r.state()==R::Stopped);intent.explicitStart();
        CHECK(r.tick(gameDetectionTime(),safe()).state==S::Stopped&&r.snapshot().episodeKey==episode&&!r.start(gameDetectionTime()));r.stop();}
    return finishTests("game runtime lifetime");
}
