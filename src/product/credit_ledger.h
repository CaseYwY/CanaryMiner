// Exact attributable posted-credit facts. No account/balance/payout/share API, sales inference or miner control.
#pragma once
#include "exact_decimal.h"
#include "market_data.h"
#include <filesystem>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace om {
enum class CreditQualification { Unsupported, SyntheticFixture };  // no production credit provider is qualified yet
enum class CreditScope { Worker, Account };
enum class CreditAsset { Unsupported, PearlMainnetNative };
enum class CreditTimeBasis { Unsupported, PostedUtcMilliseconds };
enum class CreditRevisionModel { Unsupported, ReplacementValues };
enum class CreditAmountBasis { Unsupported, PostedNetOfPoolCharges };
struct CreditSourceContract {
    CreditQualification qualification = CreditQualification::Unsupported;
    CreditAsset asset = CreditAsset::Unsupported;
    CreditScope scope = CreditScope::Worker;
    CreditTimeBasis timeBasis = CreditTimeBasis::Unsupported;
    CreditRevisionModel revisionModel = CreditRevisionModel::Unsupported;
    CreditAmountBasis amountBasis = CreditAmountBasis::Unsupported;
    std::string provider, contractVersion, scopeId, scopeGeneration;
    bool scaleDeclared = false, accountScopeAcknowledged = false;
    unsigned amountScale = 0;
    int64_t historyStartMs = 0;  // declared lower included history boundary, not inferred lifetime/inception
};
enum class CreditKind { PostedCredit, PostedAdjustment };
struct CreditRevision {
    std::string id;
    uint64_t revision = 0;
    CreditKind kind = CreditKind::PostedCredit;
    std::string reference;  // adjustments reference a known PostedCredit, never a payout/balance/another adjustment
    int64_t postedAtMs = 0;
    ExactDecimal amount;  // replacement value for this ID/revision, not a delta applied on each replay
};
struct CreditCoverage {
    int64_t fromExclusiveMs = 0, throughInclusiveMs = 0;
};
struct CreditBatch {
    CreditSourceContract binding;
    uint64_t sequence = 0;  // consecutive source snapshot/revision-stream sequence under the explicit fixture contract
    std::string snapshotId;
    int64_t sourceAsOfMs = 0, observedAtMs = 0;
    std::vector<CreditRevision> records;
    // Replaces, never silently unions with, previous completeness claims. Empty means unknown coverage, not zero income.
    std::vector<CreditCoverage> coverage;
};
// Pure bounded input checks shared by IPC/queue admission; they do not qualify a real provider or consult a store.
bool validCreditSourceContract(const CreditSourceContract &contract);
bool sameCreditSourceContract(const CreditSourceContract &a, const CreditSourceContract &b);
bool boundedCreditBatchInput(const CreditBatch &batch, int64_t nowUtcMs);
struct CreditLedgerLimits {
    size_t maxRecords = 100000, maxVersions = 400000;
    size_t maxSnapshots = 400000;
    uint64_t maxDatabaseBytes = 128 * 1024 * 1024;
};
enum class CreditLedgerStatus { Ok, Replay, StaleBatch, Invalid, Unsupported, Attribution, Conflict, SequenceGap, Clock, Capacity, Storage };
const char *creditLedgerStatusName(CreditLedgerStatus status);
enum class CreditCompleteness { Unavailable, Partial, Complete };
struct CreditTotals {
    CreditLedgerStatus status = CreditLedgerStatus::Storage;
    CreditCompleteness completeness = CreditCompleteness::Unavailable;
    CreditQualification qualification = CreditQualification::Unsupported;
    CreditScope scope = CreditScope::Worker;
    int64_t fromExclusiveMs = 0, throughInclusiveMs = 0, sourceAsOfMs = 0, observedAtMs = 0;
    uint64_t coveredMs = 0, requestedMs = 0, matchedRecords = 0;
    ExactDecimal knownPostedNet;
    std::optional<ExactDecimal> completePostedNet;
};
class CreditLedger {
   public:
    // Synchronous, single-owner API. SQLite FULLMUTEX does not make this object's cached metadata thread-safe.
    CreditLedger();
    ~CreditLedger();
    CreditLedger(const CreditLedger &) = delete;
    CreditLedger &operator=(const CreditLedger &) = delete;
    CreditLedgerStatus open(const std::filesystem::path &absoluteLocalFile, const CreditSourceContract &contract, int64_t nowUtcMs, CreditLedgerLimits limits = {});
    CreditLedgerStatus apply(const CreditBatch &batch, int64_t nowUtcMs);
    // Read-only proof for the current latest snapshot only. Never inserts/replays a missing batch.
    CreditLedgerStatus verifyLatest(const CreditBatch &batch, int64_t nowUtcMs);
    CreditTotals query(int64_t fromExclusiveMs, int64_t throughInclusiveMs, int64_t nowUtcMs);
    CreditTotals cumulative(int64_t throughInclusiveMs, int64_t nowUtcMs);
   private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
#ifdef CANARY_CREDIT_TESTING
    friend struct CreditLedgerTestPeer;
#endif
};
struct CreditReferenceValue {
    std::optional<ExactDecimal> knownUsdt, knownUsd;
    bool complete = false;  // partial references never become complete actual-dollar income
};
CreditReferenceValue creditProductionReference(const CreditTotals &totals, const MarketLatest &latest);

#ifdef CANARY_CREDIT_TESTING
// Separate crash-fixture library only; never compiled into the ordinary ledger target.
struct CreditLedgerTestPeer { static void crashBeforeCommit(CreditLedger &ledger); };
#endif
}  // namespace om
