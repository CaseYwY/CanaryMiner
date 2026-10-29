#pragma once
#include "credit_ledger.h"
#include "local_task_process.h"
namespace om {
struct CreditQuerySpec { bool cumulative = false; int64_t fromExclusiveMs = 0, throughInclusiveMs = 0; };
enum class CreditRecoveryMode { Volatile, Inspect, Submit, Retry };
enum class CreditRecoveryState { Disabled, Empty, Prepared, Confirmed, Rejected, Diverged, Unavailable };
constexpr size_t creditRecoveryRequestLimit = 48*1024; // leaves space for retained input plus totals in the 64-KiB reply
struct CreditProcessRequest {
    uint64_t generation = 0, ticket = 0;
    std::filesystem::path file;
    CreditSourceContract binding;
    CreditLedgerLimits limits;
    int64_t nowUtcMs = 0;
    std::vector<CreditQuerySpec> queries;
    std::optional<CreditBatch> batch;
    CreditRecoveryMode recovery = CreditRecoveryMode::Volatile;
};
struct CreditRecoverySnapshot {
    CreditLedgerStatus status = CreditLedgerStatus::Storage;
    CreditRecoveryState state = CreditRecoveryState::Unavailable;
    std::optional<CreditBatch> batch; // original source/observation times; never refreshed by inspection or retry
    std::optional<CreditLedgerStatus> applied;
};
struct CreditProcessReply {
    uint64_t generation = 0, ticket = 0;
    CreditLedgerStatus opened = CreditLedgerStatus::Storage;
    std::optional<CreditLedgerStatus> applied;
    std::vector<CreditTotals> totals;
    std::optional<CreditRecoverySnapshot> recovery;
    std::optional<CreditCursorCheckpoint> cursor; // committed ledger progress, separate from retained pending input
};
struct CreditProcessResult {
    LocalTaskStatus transport = LocalTaskStatus::Unavailable;
    bool writeOutcomeUnknown = false;  // helper may have committed before cancellation, timeout or lost/invalid acknowledgement
    std::optional<CreditProcessReply> reply;
};
bool encodeCreditRequest(const CreditProcessRequest &request, std::string *wire);
bool decodeCreditRequest(const std::string &wire, CreditProcessRequest *request);
bool encodeCreditReply(const CreditProcessRequest &request, const CreditProcessReply &reply, std::string *wire);
bool decodeCreditReply(const CreditProcessRequest &request, const std::string &wire, CreditProcessReply *reply);
CreditProcessReply executeCreditRequest(const CreditProcessRequest &request); // helper-side synchronous store only
// Strict v2 Inspect without a batch: both stores must already exist; no prepare/apply/settle or missing-store creation.
CreditProcessReply executeExistingCreditQuery(const CreditProcessRequest &request);
class CreditProcessClient {
   public:
    explicit CreditProcessClient(std::filesystem::path installedHelper) : process_(std::move(installedHelper)) {}
    CreditProcessResult transact(const CreditProcessRequest &request, const std::atomic<bool> &cancel, unsigned timeoutMs);
   private:
    LocalTaskProcess process_;
};
}  // namespace om
