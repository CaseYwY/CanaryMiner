// Bounded synthetic stdin oracle harness only. No source adapter, filesystem, process, device or game operations.
#include "product/game_experience.h"
#include <iostream>
int main(){using namespace om;unsigned count=0;if(!(std::cin>>count)||!count||count>256)return 2;
    for(unsigned test=0;test<count;++test){
        GameExperienceInput input;auto &p=input.plan;p.enabled=true;p.qualification=GameExperienceQualification::SyntheticFixture;
        p.identity={"fixture-game","fixture-scene","fixture-device","fixture-driver","fixture-miner","fixture-kernel","fixture-display","fixture-settings","fixture-environment","fixture-adapter"};
        p.hostWaitMs=100;p.repetitions=2;p.warmupNs=1000000;p.minimumWindowNs=1;p.maximumTestNs=3600000000000ULL;
        auto &limits=p.limits;size_t n=0,m=0;
        if(!(std::cin>>limits.minimumMeanMilliFps>>limits.minimumLowMilliFps>>limits.maximumMeanLossBp>>limits.maximumLowLossBp>>limits.maximumFrameNs>>limits.maximumP99FrameNs>>n>>m)||n<100||m<100||n>1024||m>1024)return 2;
        limits.maximumMemoryBytes=8000000000ULL;limits.maximumTemperatureMilliC=80000;
        GameExperiencePair pair;uint64_t previous=1000000000ULL,sequence=0;
        for(auto *window:{&pair.baseline,&pair.candidate}){
            window->identity=p.identity;window->session=1;window->sequence=++sequence;window->beginNs=previous;window->warmupNs=p.warmupNs;
            window->complete=window->safetyValid=window->thermalSafe=true;window->peakMemoryBytes=4000000000ULL;window->peakTemperatureMilliC=65000;
            window->framesNs.resize(window==&pair.baseline?n:m);uint64_t sum=0;
            for(auto &frame:window->framesNs){if(!(std::cin>>frame)||!frame||frame>10000000000ULL||sum>10000000000ULL-frame)return 2;sum+=frame;}
            window->endNs=window->beginNs+sum;previous=window->endNs+p.warmupNs;
        }
        pair.candidate.mining=true;pair.candidate.hostWaitMs=100;input.pairs.push_back(pair);
        for(auto *window:{&pair.baseline,&pair.candidate}){const auto duration=window->endNs-window->beginNs;window->sequence=++sequence;window->beginNs=previous;window->endNs=previous+duration;previous=window->endNs+p.warmupNs;}
        input.pairs.push_back(std::move(pair));GameExperienceComparison out;const auto result=compareGameExperience(input,&out);
        if(result!=GameExperienceStatus::Ok&&result!=GameExperienceStatus::OutsideLimits)return 3;
        std::cout<<'['<<(out.withinDeclaredLimits?1:0);
        for(const auto *metrics:{&out.trials[0].baseline,&out.trials[0].candidate}){
            std::cout<<','<<metrics->mean.frames<<','<<metrics->mean.denominatorNs<<','<<metrics->slowestOnePercentMean.frames<<','<<metrics->slowestOnePercentMean.denominatorNs<<','<<metrics->p99FrameNs<<','<<metrics->maximumFrameNs;
        }
        std::cout<<"]\n";
    }
    std::string extra;if(std::cin>>extra)return 2;
    return 0;
}
