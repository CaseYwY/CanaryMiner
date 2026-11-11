// One explicit-parent private preference helper. All disk/process I/O runs on its owned thread, never in UI callbacks.
#pragma once
#include "preferences_protocol.h"
#include "session_intent.h"
#include "local_task_process.h"
#include <array>
#include <filesystem>
#include <memory>
namespace om {
enum class PreferencesOwnerState { Dormant, Starting, Ready, Busy, NeedsInspection, Faulted, Stopped };
enum class PreferencesAdmission { Queued, Busy, NeedsInspection, Inactive, Invalid, Stale };
struct PreferencesSubmission {PreferencesAdmission status=PreferencesAdmission::Inactive;uint64_t ticket=0;};
struct PreferencesOwnerPolicy {unsigned operationTimeoutMs=5000,reapMs=250;};
struct PreferencesOwnerSnapshot {
    PreferencesOwnerState state=PreferencesOwnerState::Dormant;
    LocalTaskStatus transport=LocalTaskStatus::Unavailable;
    uint64_t submitted=0,completed=0,inspectionReview=0;
    PreferencesKind kind=PreferencesKind::Ui;
    PreferencesOperation operation=PreferencesOperation::Init;
    bool pending=false,inFlight=false,settled=false; // inFlight = background owns the request, not proof of bytes/commit
    std::array<uint64_t,8> reviews{};
    std::shared_ptr<const PreferencesReply> reply; // historical when stopped/faulted; never runtime settings authority
    std::shared_ptr<const PreferencesRequest> uncertain,acknowledgedUnknown; // exact private originals, no automatic retry
};
class PreferencesOwner {
  public:
    PreferencesOwner(std::filesystem::path helper,std::array<std::filesystem::path,8> frozenTargets,const SessionIntent &,SessionToken,PreferencesOwnerPolicy={});
    ~PreferencesOwner();
    PreferencesOwner(const PreferencesOwner &)=delete;PreferencesOwner &operator=(const PreferencesOwner &)=delete;
    bool start(); // single-use, no implicit mining start and no file/native-process wait on this control thread
    PreferencesSubmission load(PreferencesKind);
    PreferencesSubmission save(PreferencesKind,uint64_t reviewed,const std::string &canonicalDocument);
    // Consumes only the exact completed current ticket. Foreign callers cannot consume a healthy newer owner's result.
    std::shared_ptr<const PreferencesReply> takeReply(uint64_t ticket);
    // Explicit operator acknowledgement AFTER a fresh same-kind Load and complete unknown/current review.
    // Clears the block, not the historical unknown outcome; never retries or writes anything.
    bool acknowledgeInspection(PreferencesKind,uint64_t loadedReview);
    PreferencesOwnerSnapshot snapshot()const; // memory revocation only; no clock, I/O, callback, join or C++ new
    void requestStop(); // memory-only before worker drain; preserves an issued possible write
    void stop(); // after worker drain; joins this owner's thread, not a UI/render callback or hard native bound
  private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
} // namespace om
