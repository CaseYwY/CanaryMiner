// Exact aggregate checkpoint codec; no time-bin rounding, I/O, automatic activation or original-input replay.
#pragma once
#include "energy_history_model.h"
namespace om::energy_history_detail {
constexpr size_t energySnapshotLimit=4*1024*1024;
std::string retentionText(const EnergyHistoryLimits &limits);
EnergyHistoryStatus compactEnergyModel(const EnergyHistoryBinding &binding,const EnergyHistoryLimits &limits,Model *model);
struct EnergyCompactedSnapshot {
    Model model;
    std::string priorDigest,digest;
    EnergyHistoryBatch original; // the latest original always survives for pending/receipt proof
};
bool encodeEnergySnapshot(const EnergyHistoryBinding &binding,const EnergyHistoryLimits &limits,const EnergyCompactedSnapshot &snapshot,std::string *text);
bool decodeEnergySnapshot(const EnergyHistoryBinding &binding,const EnergyHistoryLimits &limits,const std::string &text,EnergyCompactedSnapshot *snapshot);
} // namespace om::energy_history_detail
