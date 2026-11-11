// Complete synthetic frame originals only. Does not observe or start a real game, worker, GPU or process.
#include "product/game_experience.h"
#include "product_test_util.h"
#include <limits>
namespace {
using namespace om;
GameExperienceInput input(){
    GameExperienceInput value;auto &plan=value.plan;plan.enabled=true;plan.qualification=GameExperienceQualification::SyntheticFixture;
    plan.identity={"fixture-game-v1","fixture-scene-v1","fixture-device","fixture-driver","fixture-miner","fixture-kernel","fixture-display","fixture-settings","fixture-environment","fixture-frames-adapter"};
    plan.hostWaitMs=100;plan.repetitions=2;plan.warmupNs=100000000ULL;plan.minimumWindowNs=1000000000ULL;plan.maximumTestNs=8000000000ULL;
    plan.limits={60000,50000,1000,2000,30000000ULL,20000000ULL,8000000000ULL,80000};
    uint64_t begin=1000000000ULL,sequence=0;
    for(unsigned repeat=0;repeat<2;++repeat){
        GameExperiencePair pair;
        for(auto *window:{&pair.baseline,&pair.candidate}){
            window->identity=plan.identity;window->session=1;window->sequence=++sequence;window->beginNs=begin;window->endNs=begin+1000000000ULL;window->warmupNs=plan.warmupNs;
            window->complete=window->safetyValid=window->thermalSafe=true;window->peakMemoryBytes=4000000000ULL;window->peakTemperatureMilliC=65000;
            window->framesNs.assign(100,10000000ULL);begin=window->endNs+plan.warmupNs;
        }
        pair.candidate.mining=true;pair.candidate.hostWaitMs=plan.hostWaitMs;value.pairs.push_back(std::move(pair));
    }
    return value;
}
bool preserved(const GameExperienceComparison &value){return value.totalElapsedNs==42&&value.trials.empty()&&value.withinDeclaredLimits&&value.statisticalConfidenceAvailable;}
void reject(GameExperienceInput value,GameExperienceStatus expected){
    GameExperienceComparison out;out.totalElapsedNs=42;out.withinDeclaredLimits=true;out.statisticalConfidenceAvailable=true;
    CHECK(compareGameExperience(value,&out)==expected);CHECK(preserved(out));
}
}
int main(){using namespace om;auto value=input();GameExperienceComparison out;
    CHECK(compareGameExperience(value,&out)==GameExperienceStatus::Ok&&out.withinDeclaredLimits&&!out.statisticalConfidenceAvailable&&out.trials.size()==2);
    CHECK(out.totalElapsedNs==4400000000ULL);
    for(const auto &trial:out.trials){CHECK(trial.withinLimits);CHECK(trial.baseline.mean.frames==100&&trial.baseline.mean.denominatorNs==1000000000ULL);
        CHECK(trial.candidate.slowestOnePercentMean.frames==1&&trial.candidate.slowestOnePercentMean.denominatorNs==10000000ULL);
        CHECK(trial.candidate.p99FrameNs==10000000ULL&&trial.candidate.maximumFrameNs==10000000ULL);}
    CHECK(compareGameExperience(value,nullptr)==GameExperienceStatus::Invalid);
    std::atomic<bool> cancel{true};GameExperienceComparison retained;retained.totalElapsedNs=42;
    CHECK(compareGameExperience(value,&retained,&cancel)==GameExperienceStatus::Cancelled&&retained.totalElapsedNs==42);
    auto bad=value;bad.plan.enabled=false;reject(bad,GameExperienceStatus::Disabled);
    bad=value;bad.plan.qualification=GameExperienceQualification::Unsupported;reject(bad,GameExperienceStatus::Unsupported);
    bad=value;bad.plan.qualification=static_cast<GameExperienceQualification>(9);reject(bad,GameExperienceStatus::Unsupported);
    for(auto field:{&GameExperienceIdentity::gameBuild,&GameExperienceIdentity::scene,&GameExperienceIdentity::device,&GameExperienceIdentity::driver,&GameExperienceIdentity::minerBuild,
                    &GameExperienceIdentity::kernel,&GameExperienceIdentity::display,&GameExperienceIdentity::settings,&GameExperienceIdentity::environment,&GameExperienceIdentity::adapter}){
        bad=value;bad.plan.identity.*field="";reject(bad,GameExperienceStatus::Invalid);
        bad=value;bad.pairs[0].candidate.identity.*field="different-fixture";reject(bad,GameExperienceStatus::Attribution);
        bad=value;bad.pairs[1].baseline.identity.*field="different-fixture";reject(bad,GameExperienceStatus::Attribution);
        bad=value;bad.plan.identity.*field=std::string(257,'a');reject(bad,GameExperienceStatus::Invalid);
    }
    for(unsigned count:{0u,1u,17u}){bad=value;bad.plan.repetitions=count;reject(bad,GameExperienceStatus::Invalid);}
    bad=value;bad.plan.hostWaitMs=1001;reject(bad,GameExperienceStatus::Invalid);
    for(auto field:{&GameExperiencePlan::warmupNs,&GameExperiencePlan::minimumWindowNs,&GameExperiencePlan::maximumTestNs}){bad=value;bad.plan.*field=0;reject(bad,GameExperienceStatus::Invalid);}
    bad=value;bad.plan.maximumTestNs=3600000000001ULL;reject(bad,GameExperienceStatus::Invalid);
    bad=value;bad.plan.minimumWindowNs=bad.plan.maximumTestNs+1;reject(bad,GameExperienceStatus::Invalid);
    bad=value;bad.plan.warmupNs=bad.plan.maximumTestNs+1;reject(bad,GameExperienceStatus::Invalid);
    for(auto field:{&GameExperienceLimits::minimumMeanMilliFps,&GameExperienceLimits::minimumLowMilliFps}){bad=value;bad.plan.limits.*field=0;reject(bad,GameExperienceStatus::Invalid);}
    bad=value;bad.plan.limits.minimumLowMilliFps=60001;reject(bad,GameExperienceStatus::Invalid);
    for(auto field:{&GameExperienceLimits::maximumMeanLossBp,&GameExperienceLimits::maximumLowLossBp}){bad=value;bad.plan.limits.*field=10001;reject(bad,GameExperienceStatus::Invalid);}
    for(auto field:{&GameExperienceLimits::maximumFrameNs,&GameExperienceLimits::maximumP99FrameNs,&GameExperienceLimits::maximumMemoryBytes}){bad=value;bad.plan.limits.*field=0;reject(bad,GameExperienceStatus::Invalid);}
    bad=value;bad.plan.limits.maximumP99FrameNs=30000001;reject(bad,GameExperienceStatus::Invalid);
    for(int temperature:{-1,0,150001}){bad=value;bad.plan.limits.maximumTemperatureMilliC=temperature;reject(bad,GameExperienceStatus::Invalid);}
    bad=value;bad.pairs.pop_back();reject(bad,GameExperienceStatus::Coverage);
    for(size_t pair=0;pair<2;++pair)for(bool candidate:{false,true}){
        const auto edit=[&](auto action,GameExperienceStatus expected){auto modified=value;action(candidate?modified.pairs[pair].candidate:modified.pairs[pair].baseline);reject(std::move(modified),expected);};
        edit([](auto &w){w.session=0;},GameExperienceStatus::Attribution);edit([](auto &w){w.sequence=0;},GameExperienceStatus::Attribution);
        edit([](auto &w){w.endNs=w.beginNs;},GameExperienceStatus::Clock);edit([](auto &w){w.endNs=w.beginNs-1;},GameExperienceStatus::Clock);
        edit([](auto &w){w.warmupNs=1;},GameExperienceStatus::Budget);edit([](auto &w){w.warmupNs=w.beginNs+1;},GameExperienceStatus::Budget);
        edit([](auto &w){w.complete=false;},GameExperienceStatus::Coverage);edit([](auto &w){w.framesNs.resize(99);},GameExperienceStatus::Coverage);
        edit([](auto &w){w.framesNs.resize(65537,1);},GameExperienceStatus::Capacity);edit([](auto &w){w.framesNs[0]=0;},GameExperienceStatus::Coverage);
        edit([](auto &w){w.framesNs[0]=UINT64_MAX;},GameExperienceStatus::Coverage);edit([](auto &w){++w.framesNs[0];},GameExperienceStatus::Coverage);
        edit([](auto &w){--w.framesNs[0];},GameExperienceStatus::Coverage);edit([](auto &w){w.peakMemoryBytes.reset();},GameExperienceStatus::Coverage);
        edit([](auto &w){w.peakTemperatureMilliC.reset();},GameExperienceStatus::Coverage);edit([](auto &w){w.peakMemoryBytes=0;},GameExperienceStatus::Unsafe);
        edit([](auto &w){w.peakTemperatureMilliC=-1;},GameExperienceStatus::Unsafe);edit([](auto &w){w.peakTemperatureMilliC=150001;},GameExperienceStatus::Unsafe);
        edit([](auto &w){w.safetyValid=false;},GameExperienceStatus::Unsafe);edit([](auto &w){w.thermalSafe=false;},GameExperienceStatus::Unsafe);edit([](auto &w){w.fault=true;},GameExperienceStatus::Unsafe);
        edit([](auto &w){w.mining=!w.mining;},GameExperienceStatus::Attribution);edit([](auto &w){w.hostWaitMs=999;},GameExperienceStatus::Attribution);
    }
    bad=value;bad.pairs[1].baseline.session=2;reject(bad,GameExperienceStatus::Attribution);
    bad=value;bad.pairs[1].candidate.sequence=3;reject(bad,GameExperienceStatus::Attribution);
    bad=value;bad.pairs[0].candidate.beginNs-=1;bad.pairs[0].candidate.endNs-=1;reject(bad,GameExperienceStatus::Attribution);
    bad=value;bad.plan.maximumTestNs=4399999999ULL;reject(bad,GameExperienceStatus::Budget);
    bad=value;++bad.pairs[0].candidate.endNs;++bad.pairs[0].candidate.framesNs[0];reject(bad,GameExperienceStatus::Coverage);
    // Equality is admitted without rounded FPS. A single nanosecond or a single bad repetition is not hidden by pooling.
    value.plan.limits.minimumMeanMilliFps=value.plan.limits.minimumLowMilliFps=100000;
    value.plan.limits.maximumMeanLossBp=value.plan.limits.maximumLowLossBp=0;
    CHECK(compareGameExperience(value,&out)==GameExperienceStatus::Ok);
    bad=value;bad.pairs[1].candidate.framesNs[0]+=1;bad.pairs[1].candidate.framesNs[1]-=1;
    CHECK(compareGameExperience(bad,&out)==GameExperienceStatus::OutsideLimits&&!out.withinDeclaredLimits&&!out.trials[1].withinLimits&&out.trials[0].withinLimits);
    // 101 frames makes the tail ceil(1.01)=2. p99 nearest rank is 100, not the maximum 101st.
    value=input();for(auto &pair:value.pairs)for(auto *w:{&pair.baseline,&pair.candidate}){w->framesNs.assign(99,9800000ULL);w->framesNs.push_back(14000000ULL);w->framesNs.push_back(15800000ULL);}
    CHECK(compareGameExperience(value,&out)==GameExperienceStatus::Ok);
    CHECK(out.trials[0].candidate.slowestOnePercentMean.frames==2&&out.trials[0].candidate.slowestOnePercentMean.denominatorNs==29800000ULL);
    CHECK(out.trials[0].candidate.p99FrameNs==14000000ULL&&out.trials[0].candidate.maximumFrameNs==15800000ULL);
    for(auto field:{&GameExperienceLimits::maximumMemoryBytes,&GameExperienceLimits::maximumFrameNs,&GameExperienceLimits::maximumP99FrameNs}){
        bad=value;bad.plan.limits.*field=field==&GameExperienceLimits::maximumMemoryBytes?3999999999ULL:13000000ULL;
        if(field==&GameExperienceLimits::maximumFrameNs){bad.plan.limits.maximumFrameNs=15000000ULL;bad.plan.limits.maximumP99FrameNs=14000000ULL;}
        CHECK(compareGameExperience(bad,&out)==GameExperienceStatus::OutsideLimits&&!out.withinDeclaredLimits);
    }
    bad=value;bad.plan.limits.maximumTemperatureMilliC=64999;CHECK(compareGameExperience(bad,&out)==GameExperienceStatus::OutsideLimits&&!out.withinDeclaredLimits);
    // Near-uint64 original clocks remain exact; only differences enter budgets/rates.
    value=input();const uint64_t offset=UINT64_MAX-10000000000ULL;
    for(auto &pair:value.pairs)for(auto *w:{&pair.baseline,&pair.candidate}){w->beginNs+=offset;w->endNs+=offset;}
    CHECK(compareGameExperience(value,&out)==GameExperienceStatus::Ok&&out.totalElapsedNs==4400000000ULL);
    // Exact paired mean threshold: 180 vs 200 frames over the same second is exactly 10% loss.
    value=input();for(auto &pair:value.pairs){
        pair.baseline.framesNs.assign(200,5000000ULL);
        pair.candidate.framesNs.assign(180,5555555ULL);pair.candidate.framesNs.back()+=100;
    }
    value.plan.limits.maximumLowLossBp=2000;
    CHECK(compareGameExperience(value,&out)==GameExperienceStatus::Ok);
    value.plan.limits.maximumMeanLossBp=999;
    CHECK(compareGameExperience(value,&out)==GameExperienceStatus::OutsideLimits&&!out.withinDeclaredLimits);
    // Exact tail regression with unchanged means: one 20ms tail vs 10ms baseline is 50% loss, not 1% frame loss.
    value=input();for(auto &pair:value.pairs){pair.candidate.framesNs[0]=20000000ULL;pair.candidate.framesNs[1]=1;pair.candidate.framesNs[2]-=1;}
    value.plan.limits.maximumLowLossBp=5000;
    CHECK(compareGameExperience(value,&out)==GameExperienceStatus::Ok);
    value.plan.limits.maximumLowLossBp=4999;
    CHECK(compareGameExperience(value,&out)==GameExperienceStatus::OutsideLimits&&!out.withinDeclaredLimits);
    // Maximum original frame/pair capacity is bounded; near-uint64 clocks above must not enter rate products directly.
    value=input();value.plan.repetitions=16;value.pairs.resize(16,value.pairs.front());uint64_t begin=1000000000ULL,sequence=0;
    for(auto &pair:value.pairs)for(auto *w:{&pair.baseline,&pair.candidate}){
        w->sequence=++sequence;w->beginNs=begin;w->endNs=begin+1000000000ULL;begin=w->endNs+value.plan.warmupNs;
        w->framesNs.assign(65536,15258);w->framesNs.back()+=1000000000ULL-15258ULL*65536;
    }
    value.plan.maximumTestNs=3600000000000ULL;CHECK(compareGameExperience(value,&out)==GameExperienceStatus::Ok&&out.trials.size()==16);
    return finishTests("offline paired original-frame experience comparison and exact declared limits");
}
