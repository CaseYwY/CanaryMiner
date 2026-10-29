#pragma once
#include "billing_input_process.h"
#include "billing_model.h"
#include "json_dom.h"
namespace om { namespace billing_input_detail {
constexpr size_t manifestLimit=32*1024,certificateLimit=4096,stageLimit=billingInputPayloadLimit+manifestLimit+16*1024+128;
bool hash(const std::string &value);
bool valid(const BillingInputManifest &manifest);
bool valid(const BillingInputRequest &request);
std::string manifestText(const BillingInputManifest &manifest);
bool parseManifest(const JsonValue &value,BillingInputManifest &manifest);
std::string certificateText(const std::optional<BillingInputCertificate> &certificate);
bool parseCertificate(const JsonValue &value,std::optional<BillingInputCertificate> &certificate);
std::string frame(const std::string &value);
bool part(const std::string &wire,size_t &offset,size_t limit,std::string &out);
std::string stageBody(const BillingInputManifest &manifest,BillingInputPhase phase,
                      const std::optional<BillingInputCertificate> &certificate,const std::string &payload);
bool proof(const BillingInputManifest &manifest,const BillingBinding &binding,const BillingLimits &limits,
           const std::optional<BillingChunk> &chunk,const BillingInputCertificate &certificate,BillingRecoverySnapshot *out=nullptr);
} } // namespace om::billing_input_detail
