// Explicit test-only stats reader. Never substitute inline production Nvml definitions:
// another translation unit can supply those definitions and bypass a loader macro.
#pragma once
#include "../src/product/supervisor_sampling.h"
namespace cpu_fixture {
class StatsOnlyReader final:public om::DisplaySampleReader {
    std::vector<om::DisplaySampleTarget> targets_;
    const uint64_t epoch_=om::nativeProcessAdmissionEpoch();
    bool current(const std::atomic<bool> &cancel)const noexcept{
        return !cancel&&om::nativeProcessEpochCurrent(epoch_)&&om::nativeProcessAdmissionEpoch()==epoch_;
    }
   public:
    explicit StatsOnlyReader(std::vector<om::DisplaySampleTarget> targets):targets_(std::move(targets)){}
    om::DisplaySampleReading read(size_t slot,const std::atomic<bool> &cancel)override{
        om::DisplaySampleReading out;if(!current(cancel)||slot>=targets_.size())return out;
        int status=0;std::string body,error;
        out.statsBegan=om::displaySampleTime();
        const bool received=om::httpGetLocal("127.0.0.1",targets_[slot].port,"/api/v1/stats",&status,&body,&error,1);
        out.statsObserved=om::displaySampleTime();if(!current(cancel))return {};
        if(received&&status==200){om::JsonValue root;om::StatsSnapshot stats;
            if(om::parseJsonStrict(body,&root,&error)&&om::statsFromJson(root,&stats,&error,out.statsBegan.monotonicMs)){
                uint64_t accepted=0;if(om::acceptedHistoryCounter(root,&accepted))out.accepted=accepted;
                out.stats=std::move(stats);
            }
        }
        // Unavailable telemetry is unknown, never a fabricated zero-power reading.
        out.powerBegan=out.powerFinished=om::displaySampleTime();return current(cancel)?out:om::DisplaySampleReading{};
    }
};
inline om::DisplaySampleFactory statsOnly(){
    return [](const std::vector<om::DisplaySampleTarget> &targets)->std::unique_ptr<om::DisplaySampleReader>{
        return std::make_unique<StatsOnlyReader>(targets);
    };
}
} // namespace cpu_fixture
