// Private opt-in enrollment. Disk and entropy operations belong exclusively to the installed local helper.
#pragma once
#include "energy_capture.h"

namespace om {
constexpr size_t energyAuthorizationLimit=24*1024;
enum class EnergyEnrollment { None, Pending, Ready };
enum class EnergyAuthorizationAction { Load, Save, Enroll, Inspect, Confirm, Provision };
enum class EnergyAuthorizationStatus { Ok, Missing, Invalid, Conflict, Storage, Clock, Selection, Disabled, NeedsReview, Entropy };
struct EnergyAuthorizationDocument {
    uint64_t revision=0;
    int64_t changedUtcMs=0;
    EnergyCapturePolicy policy; // independent consent; never inferred from session-energy preferences
    EnergyHistoryLimits limits; // immutable once an enrollment is reserved
    EnergyEnrollment enrollment=EnergyEnrollment::None;
    std::string privateNamespace,lastSessionId;
    EnergyHistoryBinding binding;
};
struct EnergyAuthorizationRequest {
    uint64_t generation=0,ticket=0;
    std::filesystem::path file;
    EnergyAuthorizationAction action=EnergyAuthorizationAction::Load;
    std::string expected; // "absent" or exact canonical document SHA-256; never last-writer-wins
    int64_t nowUtcMs=0;
    EnergyCapturePolicy policy;
    EnergyHistoryLimits limits;
    std::vector<std::string> devices; // bounded transient full UUIDs; never written to the private document
};
struct EnergyAuthorizationReply {
    uint64_t generation=0,ticket=0;
    EnergyAuthorizationStatus status=EnergyAuthorizationStatus::Storage;
    std::optional<EnergyAuthorizationDocument> document;
    std::string token;
    bool emptyStoreVerified=false,sessionIssued=false;
};
struct EnergyAuthorizationProcessResult {
    LocalTaskStatus transport=LocalTaskStatus::Unavailable;
    bool writeOutcomeUnknown=false;
    std::optional<EnergyAuthorizationReply> reply;
};
bool validEnergyAuthorizationDocument(const EnergyAuthorizationDocument &document);
bool encodeEnergyAuthorizationDocument(const EnergyAuthorizationDocument &document,std::string *text);
bool decodeEnergyAuthorizationDocument(const std::string &text,EnergyAuthorizationDocument *document);
std::filesystem::path energyAuthorizationStorePath(const std::filesystem::path &file,const EnergyAuthorizationDocument &document);
bool encodeEnergyAuthorizationRequest(const EnergyAuthorizationRequest &request,std::string *wire);
bool decodeEnergyAuthorizationRequest(const std::string &wire,EnergyAuthorizationRequest *request);
bool encodeEnergyAuthorizationReply(const EnergyAuthorizationRequest &request,const EnergyAuthorizationReply &reply,std::string *wire);
bool decodeEnergyAuthorizationReply(const EnergyAuthorizationRequest &request,const std::string &wire,EnergyAuthorizationReply *reply);
EnergyAuthorizationReply executeEnergyAuthorizationRequest(const EnergyAuthorizationRequest &request); // helper-side only
class EnergyAuthorizationProcessClient {
   public:
    explicit EnergyAuthorizationProcessClient(std::filesystem::path helper):process_(std::move(helper)){}
    EnergyAuthorizationProcessResult transact(const EnergyAuthorizationRequest &,const std::atomic<bool> &cancel,unsigned timeoutMs);
   private:
    LocalTaskProcess process_;
};
} // namespace om
