// Bounded local monetary-prefix ledger. Synchronous helper-side API only; no collector or production source admission.
#pragma once
#include "electricity.h"
#include <memory>
namespace om {
struct BillingBinding {
    std::string cycleKey;
    TariffPlan plan;
    PowerEvidence evidence; // only SyntheticFixture currently admitted; no relabelled declarations or telemetry
    int64_t cycleBeginUtcMs=0,cycleEndUtcMs=0;
};
struct BillingLimits {
    size_t maxBatches=4096,maxIntervals=131072;
    uint64_t maxDatabaseBytes=64*1024*1024;
};
struct BillingChunk {
    uint64_t sequence=0;
    std::string id;
    int64_t observedUtcMs=0; // immutable original time, not refreshed by replay/restart
    PowerTrace trace;       // contiguous exact [current through,last end); gaps/overlap never become zero
};
enum class BillingStatus { Ok, Replay, Invalid, Unsupported, Attribution, Conflict, SequenceGap, Clock, Capacity, Calculation, Storage };
enum class BillingOpen { Existing, CreateNew, Inspect }; // Inspect is SQLite READONLY and cannot apply
const char *billingStatusName(BillingStatus status);
struct BillingSnapshot {
    BillingStatus status=BillingStatus::Storage;
    uint64_t sequence=0,intervals=0;
    std::string batchId,digest;
    int64_t observedUtcMs=0;
    BillOpening opening; // exact current prefix, not a declared arbitrary initial quantity
    std::optional<ElectricityAmount> provisionalTotal,settledTotal; // empty ledger is unknown, not measured zero
    bool settlementDependent=false;
};
class BillingLedger {
   public:
    BillingLedger();
    ~BillingLedger();
    BillingLedger(const BillingLedger &)=delete;
    BillingLedger &operator=(const BillingLedger &)=delete;
    BillingStatus open(const std::filesystem::path &absoluteFile,const BillingBinding &binding,int64_t nowUtcMs,
                       BillingOpen mode=BillingOpen::Existing,BillingLimits limits={});
    BillingStatus apply(const BillingChunk &chunk,int64_t nowUtcMs);
    BillingStatus verifyCommitted(const BillingChunk &chunk,int64_t nowUtcMs)const;
    BillingSnapshot snapshot(int64_t nowUtcMs)const;
    // Same committed prefix in both complete-remainder scenarios; never commits either forecast as observed energy.
    CounterfactualBill projectRemainder(const PowerTrace &withoutMining,const PowerTrace &withMining,int64_t decisionUtcMs)const;
   private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
#ifdef CANARY_BILLING_TESTING
    friend struct BillingLedgerTestPeer;
#endif
};
#ifdef CANARY_BILLING_TESTING
struct BillingLedgerTestPeer {static void crashBeforeCommit(BillingLedger &ledger);};
#endif
} // namespace om
