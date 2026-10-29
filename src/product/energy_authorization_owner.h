// Single-flight, next-explicit-parent provisioning. No I/O on render, admission, snapshot or requestStop paths.
#pragma once
#include "energy_authorization.h"
namespace om {
enum class EnergyAuthorizationOwnerState { Stopped, Busy, Ready, NeedsReview, Faulted };
struct EnergyAuthorizationCommand {
    EnergyAuthorizationAction action=EnergyAuthorizationAction::Load;
    EnergyCapturePolicy policy;
    EnergyHistoryLimits limits;
    std::vector<std::string> devices;
};
struct EnergyAuthorizationSnapshot {
    EnergyAuthorizationOwnerState state=EnergyAuthorizationOwnerState::Stopped;
    uint64_t generation=0,revision=0,lastTicket=0;
    bool pending=false,cliNoEnergy=false,provisionAttempted=false,planAvailable=false,inspectionAvailable=false;
    bool startupPending=false; // runtime barrier only; no editable snapshot until the capture decision is frozen
    LocalTaskStatus transport=LocalTaskStatus::Unavailable;
    std::optional<EnergyAuthorizationReply> reply;
    std::shared_ptr<const EnergyAuthorizationRequest> uncertain; // retained until a separate reviewed inspection acknowledgement
};
struct EnergyAuthorizedCapture {std::filesystem::path file;EnergyCapturePlan plan;};
class EnergyAuthorizationOwner {
   public:
    EnergyAuthorizationOwner(std::filesystem::path helper,std::filesystem::path file,bool cliNoEnergy=false,unsigned timeoutMs=10000,EnergyHistoryClock clock={});
    ~EnergyAuthorizationOwner();
    EnergyAuthorizationOwner(const EnergyAuthorizationOwner &)=delete;
    EnergyAuthorizationOwner &operator=(const EnergyAuthorizationOwner &)=delete;
    bool start(uint64_t generation); // once per explicit parent; initial read only
    EnergyHistoryAdmission submit(uint64_t generation,uint64_t revision,const EnergyAuthorizationCommand &command);
    EnergyHistoryAdmission acknowledgeInspection(uint64_t generation,uint64_t revision); // memory-only; neither retries nor authorizes
    std::optional<EnergyAuthorizedCapture> takePlan(uint64_t generation,uint64_t revision); // exactly once; never from Load / uncertain receipt
    void requestStop();
    void stop();
    std::shared_ptr<const EnergyAuthorizationSnapshot> snapshot()const;
   private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace om
