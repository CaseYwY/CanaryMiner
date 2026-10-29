// Bounded helper-only staging of complete monetary inputs. Staging acknowledgements are never monetary commits.
#pragma once
#include "billing_query_process.h"
namespace om {
constexpr size_t billingInputPageBytes=48*1024,billingInputPayloadLimit=1024*1024+128*1024;
enum class BillingInputMode { CreateNew, Submit };
enum class BillingInputAction { Begin, Append, Inspect, Commit, Reconcile };
enum class BillingInputPhase { Unavailable, Receiving, Issued, Complete };
struct BillingInputManifest {
    BillingInputMode mode=BillingInputMode::CreateNew;
    std::filesystem::path file;
    size_t bindingBytes=0,chunkBytes=0;
    std::string bindingDigest,chunkDigest;
    int64_t preparedUtcMs=0;
    std::string priorRevision;
    uint64_t priorSequence=0;
    std::string priorDigest;
};
struct BillingInputRequest {
    uint64_t generation=0,ticket=0;
    BillingInputAction action=BillingInputAction::Inspect;
    BillingInputManifest manifest;
    std::string expectedRevision;
    size_t offset=0;
    std::string data;
};
struct BillingInputCertificate {
    BillingRecoveryState state=BillingRecoveryState::Unavailable; // Unavailable + Conflict proves unchanged reviewed pair, not a total
    BillingStatus result=BillingStatus::Storage;
    std::string revision,digest,committed;
    std::optional<BillingStatus> applied;
};
struct BillingInputReply {
    BillingStatus status=BillingStatus::Storage;
    BillingInputPhase phase=BillingInputPhase::Unavailable;
    std::string revision,payloadDigest;
    size_t received=0;
    std::optional<BillingInputCertificate> certificate;
    std::string data; // Inspect only: at most one raw byte page of the retained input, under the stage revision
};
struct BillingInputPlan {
    BillingQueryRequest context;
    std::optional<BillingChunk> chunk; // absent means explicit CreateNew of an EMPTY pair
    std::optional<BillingRecoverySnapshot> prior; // required for Submit; exact already reviewed journal/head
};
bool prepareBillingInput(const BillingInputPlan &plan,BillingInputManifest *manifest,std::string *payload);
bool encodeBillingInputRequest(const BillingInputRequest &request,uint64_t parentPid,std::string *wire);
bool decodeBillingInputRequest(const std::string &wire,BillingInputRequest *request,uint64_t *parentPid);
bool encodeBillingInputReply(const BillingInputRequest &request,const BillingInputReply &reply,std::string *wire);
bool decodeBillingInputReply(const BillingInputRequest &request,const std::string &wire,BillingInputReply *reply);
BillingInputReply executeBillingInputRequest(const BillingInputRequest &request);
struct BillingInputResult {
    LocalTaskStatus transport=LocalTaskStatus::Unavailable;
    bool childStarted=false,commitIssued=false; // unknown outcomes remain reviewable; neither is a checkpoint
    std::optional<BillingInputReply> reply;
    std::optional<BillingRecoverySnapshot> confirmed; // only exact Empty/Confirmed/Rejected proof
};
class BillingInputProcessClient {
   public:
    explicit BillingInputProcessClient(std::filesystem::path installedHelper);
    BillingInputResult exchange(const BillingInputRequest &request,const std::atomic<bool> &cancel,unsigned timeoutMs);
    // At most 26 flights (Begin + 24 pages + Commit), one shared deadline. Never automatically resumes an unknown flight.
    BillingInputResult transact(const BillingInputPlan &plan,const std::string &previousCompletedStage,
                                const std::atomic<bool> &cancel,unsigned timeoutMs);
   private:
    LocalTaskProcess process_;
    std::atomic<bool> busy_{false};
};
} // namespace om
