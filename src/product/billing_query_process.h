// Parent-bound readonly monetary inspection. Full originals are assembled only from one unchanged view digest.
#pragma once
#include "billing_recovery.h"
namespace om {
constexpr size_t billingQueryPageBytes=48*1024,billingQueryViewLimit=128*1024+4096;
struct BillingQueryRequest {
    uint64_t generation=0,ticket=0;
    std::filesystem::path file;
    BillingBinding binding;
    BillingLimits limits;
    int64_t nowUtcMs=0;
};
struct BillingQueryPageRequest {
    uint64_t generation=0,ticket=0;
    std::filesystem::path file;
    std::string bindingDigest,expectedView;
    int64_t nowUtcMs=0;
    size_t offset=0;
};
struct BillingQueryPageReply {
    BillingStatus status=BillingStatus::Storage;
    std::string viewDigest,data;
    size_t total=0;
};
bool encodeBillingQueryRequest(const BillingQueryPageRequest &request,uint64_t parentPid,std::string *wire);
bool decodeBillingQueryRequest(const std::string &wire,BillingQueryPageRequest *request,uint64_t *parentPid);
bool encodeBillingQueryReply(const BillingQueryPageRequest &request,const BillingQueryPageReply &reply,std::string *wire);
bool decodeBillingQueryReply(const BillingQueryPageRequest &request,const std::string &wire,BillingQueryPageReply *reply);
BillingQueryPageReply executeBillingQueryRequest(const BillingQueryPageRequest &request);
struct BillingQueryResult {
    LocalTaskStatus transport=LocalTaskStatus::Unavailable;
    std::optional<BillingRecoverySnapshot> reply;
};
class BillingQueryProcessClient {
   public:
    explicit BillingQueryProcessClient(std::filesystem::path installedHelper);
    BillingQueryResult transact(const BillingQueryRequest &request,const std::atomic<bool> &cancel,unsigned timeoutMs);
   private:
    LocalTaskProcess process_;
    std::atomic<bool> busy_{false};
};
} // namespace om
