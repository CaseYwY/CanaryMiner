// Serialized bounded local credit jobs. Queue admission is NOT a durable acknowledgement or mining authorization.
#pragma once
#include "credit_process.h"
#include <functional>
namespace om {
struct CreditOwnerPolicy { size_t maxJobs = 8, maxWireBytes = 1024*1024; unsigned operationTimeoutMs = 10000; CreditLedgerLimits ledger; };
enum class CreditOwnerState { Stopped, Starting, Ready, Busy, NeedsReconciliation, Faulted };
enum class CreditEnqueueStatus { Queued, NotReady, StaleGeneration, Full, Invalid, NeedsReconciliation };
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
    CreditEnqueueResult refresh(uint64_t generation, const std::vector<CreditQuerySpec> &queries = {});
    // Explicit reviewed recovery only: retry exact retained batch identity/content. Never run automatically after stop/fault.
    CreditEnqueueResult retryUncertain(uint64_t generation);
    void requestStop(); // short in-memory cancellation; discards queued unacknowledged jobs, no file/helper I/O here
    void stop();        // joins the background task after cancellation; invoke outside the mining/render/safety loop
    std::shared_ptr<const CreditOwnerSnapshot> snapshot() const;
   private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace om
