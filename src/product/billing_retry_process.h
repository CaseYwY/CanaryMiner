// Explicit parent-bound recovery of an already reviewed durable original. No new input/create/import authority.
#pragma once
#include "billing_query_process.h"
namespace om {
struct BillingRetryRequest {
    uint64_t generation=0,ticket=0;
    std::filesystem::path file;
    std::string bindingDigest,revision,originalDigest;
    int64_t nowUtcMs=0;
};
struct BillingRetryReply {
    BillingStatus status=BillingStatus::Storage;
    std::string confirmedRevision,committedDigest,state;
    std::optional<BillingStatus> recordedResult;
};
bool encodeBillingRetryRequest(const BillingRetryRequest &request,uint64_t parentPid,std::string *wire);
bool decodeBillingRetryRequest(const std::string &wire,BillingRetryRequest *request,uint64_t *parentPid);
bool encodeBillingRetryReply(const BillingRetryRequest &request,const BillingRetryReply &reply,std::string *wire);
bool decodeBillingRetryReply(const BillingRetryRequest &request,const std::string &wire,BillingRetryReply *reply);
BillingRetryReply executeBillingRetryRequest(const BillingRetryRequest &request);
struct BillingRetryResult {
    LocalTaskStatus transport=LocalTaskStatus::Unavailable;
    std::optional<BillingStatus> applied;
    std::optional<BillingRecoverySnapshot> confirmed; // present only after original/revision/head proof; failures require readonly inspection
};
class BillingRetryProcessClient {
   public:
    explicit BillingRetryProcessClient(std::filesystem::path installedHelper);
    BillingRetryResult transact(const BillingQueryRequest &context,const BillingRecoverySnapshot &reviewed,const std::atomic<bool> &cancel,unsigned timeoutMs);
   private:
    LocalTaskProcess process_;
    std::atomic<bool> busy_{false};
};
} // namespace om
