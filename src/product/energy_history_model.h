// Internal deterministic model/codec shared by the ledger and bounded IPC, not a sensor adapter.
#pragma once
#include "energy_ledger.h"
#include <map>
#include <set>

namespace om { struct JsonValue; }
namespace om::energy_history_detail {
constexpr int64_t lastUtcMs = 253402300799999LL;
constexpr uint64_t clockGuardMs = 2000;
constexpr size_t batchWireLimit = 48*1024;
constexpr uint64_t lifetimeBatchLimit = 1000000000000ULL;
bool instant(int64_t n);
bool identifier(const std::string &s);
std::string bindingText(const EnergyHistoryBinding &b);
bool parseBinding(const std::string &text, EnergyHistoryBinding *out);
bool parseBindingValue(const JsonValue &value, EnergyHistoryBinding *out);
std::string batchText(const EnergyHistoryBatch &b);
bool parseBatch(const std::string &text, const EnergyHistoryBinding &binding, EnergyHistoryBatch *out);
bool parseBatchValue(const JsonValue &value, const EnergyHistoryBinding &binding, EnergyHistoryBatch *out);
struct Interval { int64_t begin = 0, end = 0; unsigned channel = 0; uint32_t milliwatts = 0; };
struct Channel { std::optional<EnergyHistoryReading> last; };
struct EnergySegment { int64_t begin=0,end=0;uint64_t milliwatts=0;unsigned coveredChannels=0; };
struct Model {
    EnergyHistoryCheckpoint head;
    std::optional<EnergyHistorySession> session;
    uint64_t lastMono = 0, lastContinuity = 0;
    int64_t lastWall = 0;
    std::vector<Channel> channels;
    std::set<std::string> sessions, batchIds;
    std::map<std::pair<std::string,std::string>,EnergyResetWindow> windows;
    std::vector<Interval> intervals;
    std::vector<EnergySegment> segments; // exact aggregate piecewise power and coverage, not time-bin averages
    // Caller stages a copy for atomic rejection. No rounding, I/O or system-time inference.
    EnergyHistoryStatus advance(const EnergyHistoryBinding &binding, const EnergyHistoryBatch &batch, const EnergyHistoryLimits &limits);
    EnergyHistoryTotals query(const EnergyHistoryBinding &binding, int64_t begin, int64_t end, int64_t now) const;
};
} // namespace om::energy_history_detail
