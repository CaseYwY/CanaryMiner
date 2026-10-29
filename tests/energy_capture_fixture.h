#pragma once
#include "product/energy_capture.h"
namespace energy_capture_fixture {
constexpr int64_t T=1800000000000LL;
inline std::vector<std::string> devices(){return {"fixture.board-a","fixture.board-b"};}
inline om::EnergyCapturePlan plan(){
    om::EnergyCapturePlan p;p.policy.persistenceAuthorized=true;p.privateNamespace="0123456789abcdef0123456789abcdef";p.sessionId="fixture.current-parent";
    p.limits.compaction=om::EnergyCompactionPolicy{2,32768};
    om::makeEnergyCaptureBinding(p.privateNamespace,T,devices(),om::PowerOrigin::SyntheticFixture,&p.binding);return p;
}
inline om::EnergyHistoryRequest request(const std::filesystem::path &file){
    const auto p=plan();om::EnergyHistoryRequest r;r.generation=r.ticket=1;r.file=file;r.binding=p.binding;r.limits=p.limits;r.nowUtcMs=T+10000;r.recovery=om::EnergyRecoveryMode::Inspect;return r;
}
}
