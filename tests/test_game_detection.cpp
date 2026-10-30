#include "product/game_detection_owner.h"
#include "product_test_util.h"
#include <functional>
#include <thread>
using namespace om;
namespace {
using P=GameProbeStatus;using O=GameDetectionOwnerState;
GameProbeRequest request(const std::string &name="fixture-present"){
    GameProbeRequest r;r.enabled=true;r.generation=7;r.revision=3;r.target={"fixture.selected-game",name,GameProcessAdapter::WindowsSessionImage};r.issued=gameDetectionTime();return r;
}
bool wait(const std::function<bool()> &done,unsigned ms=5000){const auto until=std::chrono::steady_clock::now()+std::chrono::milliseconds(ms);
    do{if(done())return true;std::this_thread::sleep_for(std::chrono::milliseconds(5));}while(std::chrono::steady_clock::now()<until);return done();}
void codec(){
    auto r=request();r.privateScope=std::string(32,'1');r.issued={1793145600000LL,1000};std::string wire;uint64_t pid=0;GameProbeRequest copy;
    CHECK(encodeGameProbeRequest(r,42,&wire)&&decodeGameProbeRequest(wire,&copy,&pid)&&pid==42&&copy.target.executable==r.target.executable&&copy.revision==3);
    for(auto change:std::vector<std::function<void(GameProbeRequest &)>>{
        [](auto &q){q.generation=0;},[](auto &q){q.revision=0;},[](auto &q){q.target.gameKey.clear();},[](auto &q){q.target.executable="../game";},
        [](auto &q){q.target.executable="game.exe --run";},[](auto &q){q.target.executable="game\n";},[](auto &q){q.target.executable=std::string(129,'a');},
        [](auto &q){q.target.adapter=static_cast<GameProcessAdapter>(9);},[](auto &q){q.privateScope=std::string(32,'A');},[](auto &q){q.issued.utcMs=-1;},
        [](auto &q){q.scanBudgetMs=24;},[](auto &q){q.scanBudgetMs=1001;},[](auto &q){q.maxProcesses=0;},[](auto &q){q.maxProcesses=32769;}}){auto q=r;change(q);CHECK(!encodeGameProbeRequest(q,42,&wire));}
    CHECK(!encodeGameProbeRequest(r,0,&wire)&&!encodeGameProbeRequest(r,UINT64_MAX,&wire));CHECK(encodeGameProbeRequest(r,42,&wire));
    CHECK(!decodeGameProbeRequest(wire+"extra\n",&copy,&pid)&&!decodeGameProbeRequest(wire.substr(0,wire.size()-1),&copy,&pid));
    auto wrong=wire;wrong.replace(wrong.find("42\n"),3,"042\n");CHECK(!decodeGameProbeRequest(wrong,&copy,&pid));
    wrong=wire;wrong.replace(wrong.find("42\n"),3,"18446744073709551616\n");CHECK(!decodeGameProbeRequest(wrong,&copy,&pid));
    CHECK(!decodeGameProbeRequest(std::string(2049,'x'),&copy,&pid));
    GameProbeReply p;p.status=P::Ok;p.privateScope=r.privateScope;p.began={r.issued.utcMs+1,1001};p.finished={r.issued.utcMs+21,1021};p.matches=1;p.instanceSet=std::string(64,'a');
    GameProbeReply decoded;CHECK(encodeGameProbeReply(r,p,&wire)&&decodeGameProbeReply(r,wire,&decoded));
    const auto present=gameProbeDetection(r,p);CHECK(present.state==GamePresence::Present&&present.observed.monotonicMs==1001&&present.episodeKey=="process."+p.instanceSet);
    for(auto change:std::vector<std::function<void(GameProbeReply &)>>{
        [](auto &q){q.privateScope=std::string(32,'b');},[](auto &q){q.matches=65;},[](auto &q){q.began.utcMs=0;},[](auto &q){q.began.monotonicMs=999;},
        [](auto &q){q.finished.monotonicMs=1000;},[](auto &q){q.finished.monotonicMs=1252;},[](auto &q){q.instanceSet.clear();},
        [](auto &q){q.matches=0;},[](auto &q){q.status=P::Incomplete;}}){auto q=p;change(q);CHECK(!encodeGameProbeReply(r,q,&wire)&&gameProbeDetection(r,q).state==GamePresence::Unknown);}
    CHECK(encodeGameProbeReply(r,p,&wire));
    for(auto change:std::vector<std::function<void(GameProbeRequest &)>>{
        [](auto &q){q.generation++;},[](auto &q){q.revision++;},[](auto &q){q.target.gameKey+="-other";},[](auto &q){q.target.executable+="-other";},
        [](auto &q){q.scanBudgetMs++;},[](auto &q){q.issued.monotonicMs++;}}){auto q=r;change(q);CHECK(!decodeGameProbeReply(q,wire,&decoded));}
    CHECK(!decodeGameProbeReply(r,wire+"\n",&decoded));
    p.matches=0;p.instanceSet.clear();CHECK(encodeGameProbeReply(r,p,&wire)&&gameProbeDetection(r,p).state==GamePresence::Absent);
    for(auto status:{P::Disabled,P::Unsupported,P::Incomplete,P::Capacity,P::Clock,P::Invalid,P::Entropy}){p.status=status;CHECK(encodeGameProbeReply(r,p,&wire)&&decodeGameProbeReply(r,wire,&decoded)&&gameProbeDetection(r,p).state==GamePresence::Unknown);}
    r.enabled=false;r.privateScope.clear();p.status=P::Disabled;p.privateScope.clear();CHECK(encodeGameProbeReply(r,p,&wire)&&decodeGameProbeReply(r,wire,&decoded));
}
}
int main(int argc,char **argv){
    if(argc!=3&&!(argc==4&&std::string(argv[3])=="--require-native"))return 2;
    codec();const auto native=std::filesystem::absolute(argv[1]),fixture=std::filesystem::absolute(argv[2]);std::atomic<bool> cancel{false};
    GameProbeClient client(fixture);
    for(const auto &mode:{"fixture-present","fixture-absent","fixture-incomplete","fixture-clock"}){
        auto r=request(mode);const auto out=client.transact(r,cancel,1000);CHECK(out.transport==LocalTaskStatus::Ok&&out.reply);
        if(out.reply){const auto d=gameProbeDetection(r,*out.reply);CHECK(d.revision==r.revision&&d.generation==r.generation&&d.observed.monotonicMs>=r.issued.monotonicMs);
            CHECK(d.state==(mode==std::string("fixture-present")?GamePresence::Present:mode==std::string("fixture-absent")?GamePresence::Absent:GamePresence::Unknown));}
    }
    for(const auto &mode:{"fixture-malformed","fixture-wrong"}){const auto out=client.transact(request(mode),cancel,1000);CHECK(out.transport==LocalTaskStatus::Unavailable&&!out.reply);}
    {auto r=request("fixture-hang");r.scanBudgetMs=25;const auto start=gameDetectionTime();const auto out=client.transact(r,cancel,50);
        CHECK(out.transport==LocalTaskStatus::Timeout&&!out.reply&&gameDetectionTime().monotonicMs-start.monotonicMs<1500);}
    {auto r=request("fixture-delayed");const auto out=client.transact(r,cancel,1000);CHECK(out.reply&&gameDetectionTime().monotonicMs-gameProbeDetection(r,*out.reply).observed.monotonicMs>=140);}
    {cancel=true;CHECK(client.transact(request(),cancel,1000).transport==LocalTaskStatus::Cancelled);cancel=false;
        GameProbeClient missing(std::filesystem::path("missing"));auto r=request();r.enabled=false;const auto out=missing.transact(r,cancel,1000);CHECK(out.transport==LocalTaskStatus::Ok&&out.reply&&out.reply->status==P::Disabled);}
    // Actual native process inspection selects only this owned CPU test executable. No game is launched.
    {GameProbeClient actual(native);auto r=request(std::filesystem::path(argv[0]).filename().string());r.scanBudgetMs=1000;
#if defined(__linux__)
        r.target.adapter=GameProcessAdapter::LinuxUidNativeImage;
#endif
        const auto first=actual.transact(r,cancel,2000);
        const bool complete=first.transport==LocalTaskStatus::Ok&&first.reply&&first.reply->status==P::Ok;
        bool unavailableAllowed=false;
#if defined(__linux__)
        unavailableAllowed=argc==3&&first.transport==LocalTaskStatus::Ok&&first.reply&&(first.reply->status==P::Incomplete||first.reply->status==P::Unsupported)&&gameProbeDetection(r,*first.reply).state==GamePresence::Unknown;
#endif
        CHECK((complete&&first.reply->matches>=1)||unavailableAllowed);
        if(unavailableAllowed)std::puts("SKIP complete native scope: host visibility unavailable; Unknown verified. Run lifecycle isolation for complete presence/exit evidence.");
        if(complete){r.privateScope=first.reply->privateScope;r.revision++;r.issued=gameDetectionTime();const auto second=actual.transact(r,cancel,2000);
            CHECK(second.reply&&second.reply->status==P::Ok&&second.reply->instanceSet==first.reply->instanceSet&&second.reply->privateScope==r.privateScope);}
        r.target.executable="fixture-no-such-native-image-591162";r.revision++;r.issued=gameDetectionTime();
        if(!unavailableAllowed){const auto absent=actual.transact(r,cancel,2000);CHECK(absent.reply&&absent.reply->status==P::Ok&&absent.reply->matches==0&&gameProbeDetection(r,*absent.reply).state==GamePresence::Absent);}
        r.maxProcesses=1;r.revision++;r.issued=gameDetectionTime();const auto capped=actual.transact(r,cancel,2000);
        CHECK(capped.reply&&(capped.reply->status==P::Capacity||(unavailableAllowed&&(capped.reply->status==P::Incomplete||capped.reply->status==P::Unsupported)))&&gameProbeDetection(r,*capped.reply).state==GamePresence::Unknown);
        r.target.adapter=r.target.adapter==GameProcessAdapter::WindowsSessionImage?GameProcessAdapter::LinuxUidNativeImage:GameProcessAdapter::WindowsSessionImage;r.revision++;r.issued=gameDetectionTime();
        const auto foreign=actual.transact(r,cancel,2000);CHECK(foreign.reply&&foreign.reply->status==P::Unsupported&&gameProbeDetection(r,*foreign.reply).state==GamePresence::Unknown);
    }
    GameDetectionPolicy policy;policy.enabled=true;policy.pollMs=100;
    {GameDetectionOwner off(fixture,request().target);CHECK(!off.start(7)&&off.snapshot()->state==O::Disabled);}
    {SessionIntent intent;const auto token=intent.explicitStart();GameDetectionOwner q(fixture,request().target,policy);CHECK(q.start(token.epoch)&&!q.start(token.epoch));CHECK(wait([&]{return q.snapshot()->completed>=2;}));
        const auto before=q.snapshot();CHECK(before->state==O::Ready&&before->detection.state==GamePresence::Present&&before->detection.revision>=2);
        GameGuardPolicy gp;gp.mode=GameIntent::Pause;GameProfitBinding binding;binding.gameKey=request().target.gameKey;GameSessionGuard guard(gp,binding,intent,token);
        CHECK(guard.update(gameDetectionTime(),before->detection,{true,true,false}).state==GameGuardState::PausedPolicy);
        q.requestStop();const auto stopped=q.snapshot();CHECK(stopped->state==O::Stopped&&stopped->detection.state==GamePresence::Unknown&&!stopped->pending);q.stop();
        CHECK(!q.start(8)&&q.snapshot()->completed==stopped->completed&&before->detection.state==GamePresence::Present);}
    {auto p=policy;p.helperTimeoutMs=2000;GameDetectionOwner q(fixture,request("fixture-hang").target,p);CHECK(q.start(7)&&wait([&]{return q.snapshot()->pending;}));
        const auto begin=gameDetectionTime();q.requestStop();CHECK(q.snapshot()->state==O::Stopped);q.stop();CHECK(gameDetectionTime().monotonicMs-begin.monotonicMs<1000&&!q.start(7));}
    {GameDetectionOwner q(fixture,request("fixture-incomplete").target,policy);CHECK(q.start(7)&&wait([&]{return q.snapshot()->completed>=1;}));CHECK(q.snapshot()->state==O::Waiting&&q.snapshot()->detection.state==GamePresence::Unknown);q.stop();}
    {GameDetectionOwner q(fixture,request("fixture-clock").target,policy);CHECK(q.start(7)&&wait([&]{return q.snapshot()->state==O::ClockFault;}));CHECK(!q.start(7)&&q.snapshot()->detection.state==GamePresence::Unknown);q.stop();}
    {std::atomic<bool> rollback{false};GameDetectionOwner q(fixture,request().target,policy,[&]{auto t=gameDetectionTime();if(rollback)t.utcMs-=10000;return t;});
        CHECK(q.start(7)&&wait([&]{return q.snapshot()->completed>=1;}));rollback=true;CHECK(wait([&]{return q.snapshot()->state==O::ClockFault;}));CHECK(!q.start(8));q.stop();}
    {GameDetectionOwner q(fixture,request().target,policy);q.requestStop();CHECK(!q.start(7)&&q.snapshot()->state==O::Stopped);}
    return finishTests("isolated game detection");
}
