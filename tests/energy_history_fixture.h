#pragma once
#include "product/energy_history_process.h"
namespace energy_history_fixture {
constexpr int64_t T=1800000000000LL;
inline om::EnergyHistoryBinding binding(){return {{om::PowerBasis::GpuBoard,om::PowerOrigin::SyntheticFixture,"fixture.selected-boards","fixture.milliwatts"},{"fixture.channel-A","fixture.channel-B"},T};}
inline om::EnergyHistoryReading reading(unsigned channel,uint64_t elapsed,std::optional<uint32_t> mw,uint64_t continuity=0){return {channel,T+static_cast<int64_t>(elapsed),100+elapsed,continuity,mw};}
inline om::EnergyHistoryBatch first(){
    om::EnergyHistoryBatch b;b.sequence=1;b.id="fixture.batch-1";b.observedAtMs=T+2000;b.sessionId="fixture.parent-1";b.start=om::EnergyHistorySession{b.sessionId,T,100,5000};
    b.readings={reading(0,0,123456),reading(1,0,0),reading(0,1000,200000),reading(1,1000,std::nullopt),reading(0,2000,std::nullopt),reading(1,2000,100000)};
    b.windows={{"billing","period-1","rule-1",T,T+1500,false,0},{"billing","period-2","rule-1",T+1500,T+4000,false,0},
        {"annual-tier","period-1","rule-1",T,T+4000,true,3600}};
    return b;
}
inline om::EnergyHistoryBatch second(){
    om::EnergyHistoryBatch b;b.sequence=2;b.id="fixture.batch-2";b.observedAtMs=T+3000;b.sessionId="fixture.parent-1";
    b.readings={reading(0,3000,1000),reading(1,3000,1000)};return b;
}
inline om::EnergyHistoryRequest request(const std::filesystem::path &file,std::optional<om::EnergyHistoryBatch> batch={}){
    om::EnergyHistoryRequest r;r.generation=r.ticket=1;r.file=file;r.binding=binding();r.nowUtcMs=T+20000;r.batch=std::move(batch);
    r.queries={{T,T+4000,"",""},{T,T+1500,"billing","period-1"},{T+1500,T+4000,"billing","period-2"},{T,T+4000,"annual-tier","period-1"}};return r;
}
} // namespace energy_history_fixture
