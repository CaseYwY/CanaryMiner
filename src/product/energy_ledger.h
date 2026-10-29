// Local observed-power history. No declared opening quantities, inferred device identity or billing activation.
#pragma once
#include "electricity.h"
#include <filesystem>
#include <memory>

namespace om {
struct EnergyHistoryBinding {
    PowerEvidence evidence;
    std::vector<std::string> channels; // stable PRIVATE caller-bound channel keys, not rediscovered device ordinals
    int64_t historyBeginUtcMs = 0;
};
struct EnergyHistorySession {
    std::string id; // fresh explicit identity across parent lifetimes; never the in-memory owner generation
    int64_t anchorUtcMs = 0;
    uint64_t anchorMonoMs = 0;
    unsigned maximumGapMs = 0;
};
struct EnergyHistoryReading {
    unsigned channel = 0;
    int64_t utcMs = 0;
    uint64_t monotonicMs = 0, continuity = 0; // advance continuity after ANY unrecorded observation; never bridge a dropped batch
    std::optional<uint32_t> milliwatts;
};
struct EnergyResetWindow {
    std::string registerId, periodId, ruleVersion;
    int64_t beginUtcMs = 0, endUtcMs = 0; // explicit half-open UTC boundaries; no automatic recurring calendar
    bool fixedStandardTime = false;
    int offsetSeconds = 0; // provenance: explicit UTC or fixed STANDARD offset, no OS/IANA fallback
};
struct EnergyHistoryBatch {
    uint64_t sequence = 0;
    std::string id;
    int64_t observedAtMs = 0; // immutable input observation time, not renewed on retry
    std::optional<EnergyHistorySession> start;
    std::string sessionId; // required for readings/start, empty for a reset-window-only declaration
    std::vector<EnergyHistoryReading> readings;
    std::vector<EnergyResetWindow> windows;
};
struct EnergyHistoryLimits {
    size_t maxBatches = 4096, maxReadings = 131072, maxSessions = 1024, maxWindows = 4096;
    uint64_t maxDatabaseBytes = 64*1024*1024;
};
enum class EnergyHistoryStatus { Ok, Replay, Invalid, Attribution, Conflict, SequenceGap, Clock, Capacity, Storage };
enum class EnergyHistoryOpen { Existing, CreateNew }; // never recreate missing/corrupt history implicitly
enum class EnergyHistoryCoverage { Unavailable, Partial, Complete };
const char *energyHistoryStatusName(EnergyHistoryStatus status);
bool validEnergyHistoryBinding(const EnergyHistoryBinding &binding);
bool validEnergyHistoryLimits(const EnergyHistoryLimits &limits);
bool boundedEnergyHistoryBatch(const EnergyHistoryBinding &binding, const EnergyHistoryBatch &batch, int64_t nowUtcMs);
struct EnergyHistoryCheckpoint {
    EnergyHistoryStatus status = EnergyHistoryStatus::Storage;
    uint64_t sequence = 0, readingCount = 0, sessionCount = 0, windowCount = 0;
    std::string batchId, sessionId;
    int64_t observedAtMs = 0, throughUtcMs = 0;
};
struct EnergyHistoryTotals {
    EnergyHistoryStatus status = EnergyHistoryStatus::Storage;
    EnergyHistoryCoverage coverage = EnergyHistoryCoverage::Unavailable;
    int64_t beginUtcMs = 0, endUtcMs = 0, observedAtMs = 0;
    uint64_t coveredDeviceMs = 0, requestedDeviceMs = 0;
    ElectricityAmount knownKwh;
    std::optional<ElectricityAmount> completeKwh;
};
class EnergyHistoryLedger {
   public:
    EnergyHistoryLedger();
    ~EnergyHistoryLedger();
    EnergyHistoryLedger(const EnergyHistoryLedger &) = delete;
    EnergyHistoryLedger &operator=(const EnergyHistoryLedger &) = delete;
    // Synchronous single-owner API; use the isolated helper/owner outside storage tests.
    EnergyHistoryStatus open(const std::filesystem::path &file, const EnergyHistoryBinding &binding, int64_t nowUtcMs,
                             EnergyHistoryOpen mode = EnergyHistoryOpen::Existing, EnergyHistoryLimits limits = {});
    EnergyHistoryStatus apply(const EnergyHistoryBatch &batch, int64_t nowUtcMs);
    EnergyHistoryCheckpoint checkpoint(int64_t nowUtcMs) const;
    EnergyHistoryTotals query(int64_t beginUtcMs, int64_t endUtcMs, int64_t nowUtcMs) const;
    EnergyHistoryTotals registerTotal(const std::string &registerId, const std::string &periodId, int64_t throughUtcMs, int64_t nowUtcMs) const;
   private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
#ifdef CANARY_ENERGY_HISTORY_TESTING
    friend struct EnergyHistoryTestPeer;
#endif
};
#ifdef CANARY_ENERGY_HISTORY_TESTING
struct EnergyHistoryTestPeer { static void crashBeforeCommit(EnergyHistoryLedger &ledger); };
#endif
} // namespace om
