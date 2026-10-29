// Readonly restart reconstruction of a staged input, selected by an already approved complete binding.
#pragma once
#include "billing_input_process.h"
namespace om {
constexpr size_t billingInputViewLimit=128*1024+32*1024+16*1024+128;
struct BillingInputSnapshot {
    BillingStatus status=BillingStatus::Storage;
    bool present=false; // false with Ok is a proven absent stage, never evidence about monetary totals
    bool replacementPending=false; // an existing .tmp slot is unresolved even when the main stage is absent
    BillingInputManifest manifest;
    BillingInputPhase phase=BillingInputPhase::Unavailable;
    std::string revision,originalPrefix; // private exact received chunk bytes; may be incomplete UTF-8/JSON
    size_t received=0;
    std::optional<BillingChunk> original; // only a complete, canonical staged chunk
    std::optional<BillingInputCertificate> certificate;
    std::optional<BillingRecoverySnapshot> outcome; // stored stage proof, not a fresh monetary-ledger query
};
bool validBillingInputQuery(const BillingQueryPageRequest &request);
bool encodeBillingInputQueryRequest(const BillingQueryPageRequest &request,uint64_t parentPid,std::string *wire);
bool decodeBillingInputQueryRequest(const std::string &wire,BillingQueryPageRequest *request,uint64_t *parentPid);
bool encodeBillingInputQueryReply(const BillingQueryPageRequest &request,const BillingQueryPageReply &reply,std::string *wire);
bool decodeBillingInputQueryReply(const BillingQueryPageRequest &request,const std::string &wire,BillingQueryPageReply *reply);
BillingQueryPageReply executeBillingInputQuery(const BillingQueryPageRequest &request);
struct BillingInputQueryResult {
    LocalTaskStatus transport=LocalTaskStatus::Unavailable;
    std::optional<BillingInputSnapshot> reply;
};
class BillingInputQueryClient {
   public:
    explicit BillingInputQueryClient(std::filesystem::path installedHelper);
    BillingInputQueryResult transact(const BillingQueryRequest &context,const std::atomic<bool> &cancel,unsigned timeoutMs);
   private:
    LocalTaskProcess process_;
    std::atomic<bool> busy_{false};
};
} // namespace om
