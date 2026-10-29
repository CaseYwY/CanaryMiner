// Serialized bounded local credit jobs. Queue admission is NOT a durable acknowledgement or mining authorization.
#pragma once
#include "credit_process.h"
#include <functional>
namespace om {
struct CreditOwnerPolicy {
    size_t maxJobs = 8, maxWireBytes = 1024*1024;
    unsigned operationTimeoutMs = 10000;
    CreditLedgerLimits ledger;
    bool persistentRecovery = true; // default helper-side journal; false retains the explicit volatile compatibility contract
};
enum class CreditOwnerState { Stopped, Starting, Ready, Busy, NeedsReconciliation, Faulted };
enum class CreditEnqueueStatus { Queued, NotReady, StaleGeneration, Full, Invalid, NeedsReconciliation, ReviewChanged };
struct CreditEnqueueResult { CreditEnqueueStatus status = CreditEnqueueStatus::NotReady; uint64_t ticket = 0; };
struct CreditOwnerSnapshot {
    CreditOwnerState state = CreditOwnerState::Stopped;
    CreditSourceContract binding;
    uint64_t generation = 0, revision = 0, lastAcceptedTicket = 0, lastCompletedTicket = 0, lastSuccessfulTicket = 0, droppedJobs = 0;
    size_t pendingJobs = 0, reservedWireBytes = 0;
    LocalTaskStatus transport = LocalTaskStatus::Unavailable;
    bool writeOutcomeUnknown = false;
    uint64_t uncertainSequence = 0;
    std::string uncertainSnapshotId;
    CreditRecoveryState recoveryState = CreditRecoveryState::Disabled;
    bool persistentRecovery = false;
    // Aliased immutable original input retained by the uncertain job, also available after a lost/invalid reply.
    // Private local review only: never log/export this or infer source progress from its existence.
    std::shared_ptr<const CreditBatch> uncertainBatch;
    uint64_t durableSequence = 0;
    std::string durableSnapshotId;
    std::optional<CreditProcessReply> reply; // immutable point-in-time results, not fresh income solely because read recently
};
using CreditOwnerClock = std::function<int64_t()>; // pure UTC millisecond clock; not a callback for disk/provider work
class CreditLedgerOwner {
   public:
    CreditLedgerOwner(std::filesystem::path installedHelper, std::filesystem::path file, CreditSourceContract binding,
                      CreditOwnerPolicy policy = {}, CreditOwnerClock clock = {});
    ~CreditLedgerOwner();
    CreditLedgerOwner(const CreditLedgerOwner &) = delete;
    CreditLedgerOwner &operator=(const CreditLedgerOwner &) = delete;
    bool start(uint64_t generation); // explicit nonzero increasing generation; performs only an async preflight query
    CreditEnqueueResult submit(uint64_t generation, const CreditBatch &batch, const std::vector<CreditQuerySpec> &queries = {});
    // Cursor streams admit one page from the reviewed committed checkpoint; no speculative next-page queue.
    CreditEnqueueResult submitPage(uint64_t generation,uint64_t revision,const CreditBatch &batch,const std::vector<CreditQuerySpec> &queries = {});
    CreditEnqueueResult refresh(uint64_t generation, const std::vector<CreditQuerySpec> &queries = {});
    // Explicit reviewed recovery only: retry exact retained batch identity/content. Never run automatically after stop/fault.
    CreditEnqueueResult retryUncertain(uint64_t generation);
    // Native review must bind to the exact observed revision; check and admission happen under the same owner mutex.
    CreditEnqueueResult retryReviewed(uint64_t generation, uint64_t revision);
    void requestStop(); // short in-memory cancellation; discards queued unacknowledged jobs, no file/helper I/O here
    void stop();        // joins the background task after cancellation; invoke outside the mining/render/safety loop
    std::shared_ptr<const CreditOwnerSnapshot> snapshot() const;
   private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace om
