#pragma once
#include "billing_fixture.h"
#include "product/energy_history_model.h"
#include "product/hash.h"
namespace billing_source_fixture {
using namespace om;
using electricity_fixture::T;
using electricity_fixture::d;
constexpr int64_t now=T+10000;
inline BillingBinding binding(const std::filesystem::path &source,bool compact=false){
    auto b=billing_fixture::binding();b.cycleEndUtcMs=T+4000;b.plan=electricity_fixture::plan(T,T+4000);
    b.source=BillingEnergySource{source,{b.evidence,{"fixture.meter-channel"},T},{}};
    if(compact)b.source->limits.compaction=EnergyCompactionPolicy{2,32768};
    return b;
}
inline EnergyHistoryRequest input(const BillingBinding &b,unsigned seq,uint32_t initial=123456){
    EnergyHistoryRequest r;r.generation=r.ticket=1;r.file=b.source->file;r.binding=b.source->binding;r.limits=b.source->limits;
    r.nowUtcMs=now;r.recovery=EnergyRecoveryMode::Submit;r.mode=seq==1?EnergyHistoryOpen::CreateNew:EnergyHistoryOpen::Existing;
    EnergyHistoryBatch batch;batch.sequence=seq;batch.id=r.limits.compaction?"energy."+std::to_string(seq):"fixture.energy."+std::to_string(seq);batch.sessionId="fixture.source-session";batch.observedAtMs=T+seq*1000;
    if(seq==1){batch.start=EnergyHistorySession{batch.sessionId,T,0,5000};batch.readings.push_back({0,T,0,0,initial});}
    batch.readings.push_back({0,T+seq*1000,seq*1000,0,100000u*(seq+1)});r.batch=std::move(batch);return r;
}
inline EnergyHistoryAnchor anchor(const EnergyHistoryCheckpoint &c){return {c.sequence,c.digest};}
inline EnergyTraceRequest query(const BillingBinding &b,EnergyHistoryAnchor anchor,int64_t begin=T,int64_t end=T+1000){
    EnergyTraceRequest r;r.source=input(b,1);r.source.batch.reset();r.source.mode=EnergyHistoryOpen::Existing;r.source.recovery=EnergyRecoveryMode::Inspect;r.source.queries={{begin,end,"",""}};r.anchor=std::move(anchor);return r;
}
inline std::filesystem::path journal(std::filesystem::path p){p+=".energy-recovery.json";return p;}
} // namespace billing_source_fixture
