#pragma once
#include "credit_ledger.h"
#include "local_task_process.h"
namespace om {
struct CreditQuerySpec { bool cumulative = false; int64_t fromExclusiveMs = 0, throughInclusiveMs = 0; };
struct CreditProcessRequest {
    uint64_t generation = 0, ticket = 0;
    std::filesystem::path file;
    CreditSourceContract binding;
    CreditLedgerLimits limits;
    int64_t nowUtcMs = 0;
    std::vector<CreditQuerySpec> queries;
    std::optional<CreditBatch> batch;
};
struct CreditProcessReply {
    uint64_t generation = 0, ticket = 0;
    CreditLedgerStatus opened = CreditLedgerStatus::Storage;
    std::optional<CreditLedgerStatus> applied;
    std::vector<CreditTotals> totals;
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
class CreditProcessClient {
   public:
    explicit CreditProcessClient(std::filesystem::path installedHelper) : process_(std::move(installedHelper)) {}
    CreditProcessResult transact(const CreditProcessRequest &request, const std::atomic<bool> &cancel, unsigned timeoutMs);
   private:
    LocalTaskProcess process_;
};
}  // namespace om
