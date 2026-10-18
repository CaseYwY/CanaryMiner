#pragma once
#include "pearl_private_execution.h"
#include "pearl_run_session.h"
#include "pearl_sustained_duration.h"
#include "pearl_generic_native_evidence.h"
namespace om {
// Fixed schema only: this is a final accounting snapshot, never proof of a
// future process exit or pool qualification. Caller must close writer/check rc.
class PearlNativeCompletionSink final {
 public:
    explicit PearlNativeCompletionSink(PearlLocalRetirementWriter &writer) : writer_(writer) {}
    PearlNativeCompletionSink(const PearlNativeCompletionSink &)=delete;
    PearlNativeCompletionSink &operator=(const PearlNativeCompletionSink &)=delete;
    bool finish(const char *runId,const PearlRunFinalEvidence &final,
        const PearlEvidenceDigest &local,uint64_t localRecords,
        uint64_t hostInvalid,uint64_t submitFailed,uint64_t work,uint64_t attempts,
        unsigned acceptedQuota=0,const PearlSustainedFinal *sustained=nullptr,
        const PearlNativeSelection *selection=nullptr,
        PearlPrivateExecution privateMode=PearlPrivateExecution::Disabled,
        const PearlPrivateExecutionStats *privateStats=nullptr,
        PearlPublicExecution publicMode=PearlPublicExecution::Disabled,
        const PearlPrivateExecutionStats *publicStats=nullptr,bool production=false) noexcept {
    if (attempted_) { reentered_=true; return false; }
    attempted_=true;
    const auto &p=final.pool; const auto &o=final.owner; const auto &s=final.source;
    const auto &l=o.liveness; const auto &g=o.progress;
    const bool publicExecution=pearlPublicEnabled(publicMode);
    const bool public24h=sustained && sustained->policy.public24h;
    if (production) {
#ifdef CANARY_PEARL_GUARD_RETIREMENT_EXPERIMENT
        if (acceptedQuota || sustained || privateMode!=PearlPrivateExecution::Disabled ||
            publicMode!=PearlPublicExecution::Disabled || privateStats || publicStats || !selection ||
            selection->m!=16384 || selection->n!=131072 || selection->k!=8192 || selection->rank!=128 ||
            !final.guard.complete || final.guard.cap!=2147483647) return false;
#else
        return false;
#endif
    }
    if (public24h && (!publicExecution || publicMode!=PearlPublicExecution::Serial)) return false;
    if(publicMode==PearlPublicExecution::Invalid || (publicExecution ?
        (!pearlPublicCompiled(publicMode) || privateMode!=PearlPrivateExecution::Disabled || privateStats || !selection ||
         !pearlPublicSelectionValid(*selection,publicMode) || !publicStats ||
         !publicStats->terminal(pearlPublicAccountingMode(publicMode),attempts) ||
         !((acceptedQuota==3 && !sustained) || (!acceptedQuota && sustained && sustained->policy.publicRun && pearlSustainedPolicyValid(sustained->policy)))) : publicStats!=nullptr))return false;
    const bool privateExecution=pearlPrivateEnabled(privateMode);
    if (privateMode==PearlPrivateExecution::Invalid ||
        (privateExecution ? (!pearlPrivateCompiled(privateMode) || !selection || !pearlPrivateSelectionValid(*selection,privateMode) ||
            !privateStats || !privateStats->terminal(privateMode,attempts) ||
            (acceptedQuota && acceptedQuota!=2) ||
            (sustained && sustained->policy.publicRun)) : privateStats!=nullptr)) return false;
    if (selection && (!(privateExecution?pearlPrivateSelectionValid(*selection,privateMode):pearlGenericSelectionMode(*selection)!=nullptr) ||
            (!acceptedQuota && !sustained && !production) ||
            !pearlGenericWorkConserved(selection->workUnitsPerAttempt,work,attempts))) return false;
    if (sustained && (acceptedQuota || !pearlSustainedFinalValid(*sustained,p.accepted) ||
            (!selection && (
            attempts>UINT64_MAX/1048576 || work!=attempts*1048576)))) return false;
    if (acceptedQuota && (acceptedQuota>10 || p.accepted!=acceptedQuota ||
            (!selection && (attempts>UINT64_MAX/1048576 || work!=attempts*1048576)))) return false;
    if (!pearlEvidenceRunIdValid(runId) || !final.complete || !local.closed || !final.wire.closed ||
        localRecords>(production?uint64_t(INT64_MAX):4096) || !local.bytes ||
        local.bytes>(production?uint64_t(INT64_MAX):8ULL*1024*1024) ||
        final.wire.bytes>(production?uint64_t(INT64_MAX):128ULL*1024*1024) ||
        localRecords!=g.submissionFailures || localRecords!=l.submissionFailures ||
        l.possiblyDeliveredUnknowns>UINT64_MAX-localRecords ||
        submitFailed!=localRecords+l.possiblyDeliveredUnknowns || attempts!=g.attemptsCompleted ||
        !p.conserved() || p.pending() || !g.invariantsHold() || !l.submissionAccountingHolds() ||
        o.sourceQuarantined || o.sourceIntegrityFlags || o.sourceEventsInvalid ||
        o.sourceRefusedEvents || o.sourceMailboxFaultBits || o.terminalEvidenceDropped ||
        o.terminalEvidenceCount || o.terminalEvidenceObserved!=o.terminalEvidenceConsumed ||
        !s.accountingHolds() || s.failClosed || s.faultBits || s.refusedEvents || s.queued ||
        s.submitInFlight || s.transportConnected || s.issuedJobBound || s.activeSessionToken ||
        !l.queryTimeValid || l.connection!=PearlConnectionState::Disconnected ||
        p.submitted!=g.submitted || p.acknowledged!=g.acknowledged || p.accepted!=g.accepted ||
        p.rejected!=g.rejected || p.stale!=g.stale || p.unknown!=g.unknown ||
        p.acceptedSuperseded!=g.acceptedSuperseded)
        return false;
    const char *guardSchema=nullptr;
#ifdef CANARY_PEARL_GUARD_RETIREMENT_EXPERIMENT
    if (p.guardRetired!=g.guardRetired || p.guardRetired!=l.guardRetired ||
            (p.guardRetired && !final.guard.complete)) return false;
    if (final.guard.complete) {
        const auto &r=final.guard;
        if (privateExecution && r.cap!=20) return false;
        if (publicExecution && r.cap!=(public24h?4096U:sustained?256U:20U)) return false;
        if ((!acceptedQuota && !sustained && !production) || r.reserved!=p.submitted || r.retired!=p.guardRetired ||
            !r.cap || r.cap>(production?2147483647U:public24h?4096U:256U) || r.reserved>r.cap || r.stopMs>r.drainMs ||
            p.rejected || p.unknown || (p.stale && !r.staleContinuation) ||
            p.accepted+p.stale+p.guardRetired!=p.submitted) return false;
        const char digits[]="0123456789abcdef";
        for (unsigned i=0;i<16;++i)
            if (runId[2*i]!=digits[r.run[i]>>4] || runId[2*i+1]!=digits[r.run[i]&15]) return false;
        if (sustained && (r.stopMs!=static_cast<uint64_t>(sustained->stopNs/1000000) ||
                r.drainMs>static_cast<uint64_t>(sustained->drainNs/1000000))) return false;
        guardSchema=production?"pearl-native-production-final-v1":"pearl-native-final-snapshot-v5";
    }
#endif
    if (privateExecution) {
        if (!guardSchema) return false;
        guardSchema="pearl-native-final-snapshot-v6";
    }
    if(publicExecution) { if(!guardSchema)return false;guardSchema="pearl-native-final-snapshot-v7"; }
    char localHex[65],wireHex[65],line[8192];
    pearlEvidenceHex(local.digest.data(),localHex); pearlEvidenceHex(final.wire.digest.data(),wireHex);
    int n=std::snprintf(line,sizeof(line),
        "{\"schema\":\"%s\",\"run_id\":\"%s\","
        "\"stop_kind\":\"%s\",\"native_exit_required\":0,"
        "\"local_stream_closed\":true,\"wire_stream_closed\":true,"
        "\"search_open\":false,\"proofs_active\":0,\"submit_in_flight\":false,"
        "\"source_disconnected\":true,\"local_b3\":\"%s\",\"wire_b3\":\"%s\"",
        guardSchema ? guardSchema : selection ? "pearl-native-final-snapshot-v4" : sustained ? "pearl-native-final-snapshot-v3" :
            acceptedQuota ? "pearl-native-final-snapshot-v2" : "pearl-native-final-snapshot-v1",
        runId,production ? "operator_stop" : sustained ? "sustained_duration" : acceptedQuota ? "accepted_quota" : "stop_requested",localHex,wireHex);
    if (n<=0 || static_cast<std::size_t>(n)>=sizeof(line)) return false;
    std::size_t used=static_cast<std::size_t>(n);
    auto field=[&](const char *name,uint64_t value) noexcept {
        if (value==UINT64_MAX) return false; // no saturation ambiguity
        const int count=std::snprintf(line+used,sizeof(line)-used,",\"%s\":%llu",name,
                                     static_cast<unsigned long long>(value));
        if (count<=0 || static_cast<std::size_t>(count)>=sizeof(line)-used) return false;
        used+=static_cast<std::size_t>(count); return true;
    };
#define PEARL_FINAL_FIELD(name,value) if (!field(name,value)) return false
#ifdef CANARY_PEARL_GUARD_RETIREMENT_EXPERIMENT
    if (final.guard.complete) {
        char sessionHex[33]; const char digits[]="0123456789abcdef";
        for (unsigned i=0;i<16;++i) {
            sessionHex[i*2]=digits[final.guard.session[i]>>4];
            sessionHex[i*2+1]=digits[final.guard.session[i]&15];
        }
        sessionHex[32]='\0';
        const int count=std::snprintf(line+used,sizeof(line)-used,",\"guard_session_id\":\"%s\"",sessionHex);
        if (count<=0 || static_cast<std::size_t>(count)>=sizeof(line)-used) return false;
        used+=static_cast<std::size_t>(count);
        PEARL_FINAL_FIELD("guard_retired",p.guardRetired);
        PEARL_FINAL_FIELD("guard_reserved",final.guard.reserved);
        PEARL_FINAL_FIELD("guard_cap",final.guard.cap);
        PEARL_FINAL_FIELD("guard_stop_ms",final.guard.stopMs);
        PEARL_FINAL_FIELD("guard_drain_ms",final.guard.drainMs);
    }
#endif
    if (selection) {
        const int count=std::snprintf(line+used,sizeof(line)-used,
            ",\"native_evidence_mode\":\"%s\",\"kernel_id\":\"%.*s\",\"tile_id\":\"%.*s\"",
            publicExecution?pearlPublicProfile(publicMode):privateExecution?pearlPrivateProfile(privateMode):pearlGenericSelectionMode(*selection),static_cast<int>(selection->kernelId.size()),selection->kernelId.data(),
            static_cast<int>(selection->tileId.size()),selection->tileId.data());
        if (count<=0 || static_cast<std::size_t>(count)>=sizeof(line)-used) return false;
        used+=static_cast<std::size_t>(count);
        PEARL_FINAL_FIELD("selection_sm",selection->sm);
        PEARL_FINAL_FIELD("selection_m",selection->m); PEARL_FINAL_FIELD("selection_n",selection->n);
        PEARL_FINAL_FIELD("selection_k",selection->k); PEARL_FINAL_FIELD("selection_rank",selection->rank);
        PEARL_FINAL_FIELD("preparation_plan_id",selection->preparationPlanId);
        PEARL_FINAL_FIELD("work_units_per_attempt",selection->workUnitsPerAttempt);
    }
    if (privateExecution) {
        const int count=std::snprintf(line+used,sizeof(line)-used,",\"private_execution\":\"%s\"",pearlPrivateName(privateMode));
        if (count<=0 || static_cast<std::size_t>(count)>=sizeof(line)-used) return false;
        used+=static_cast<std::size_t>(count);
        PEARL_FINAL_FIELD("lookahead_prepared",privateStats->prepared);
        PEARL_FINAL_FIELD("lookahead_promoted",privateStats->promoted);
        PEARL_FINAL_FIELD("lookahead_discarded",privateStats->discarded);
        PEARL_FINAL_FIELD("lookahead_ready",privateStats->ready);
    }
    if(publicExecution) {
        const int count=std::snprintf(line+used,sizeof(line)-used,",\"public_execution\":\"%s\"",pearlPublicName(publicMode));
        if(count<=0 || static_cast<std::size_t>(count)>=sizeof(line)-used)return false;
        used+=static_cast<std::size_t>(count);
        PEARL_FINAL_FIELD("lookahead_prepared",publicStats->prepared);
        PEARL_FINAL_FIELD("lookahead_promoted",publicStats->promoted);
        PEARL_FINAL_FIELD("lookahead_discarded",publicStats->discarded);
        PEARL_FINAL_FIELD("lookahead_ready",publicStats->ready);
    }
    if (acceptedQuota) { PEARL_FINAL_FIELD("accepted_quota",acceptedQuota); }
    if (sustained) {
        const int count=std::snprintf(line+used,sizeof(line)-used,",\"sustained_mode\":\"%s\"",
            pearlSustainedMode(sustained->policy));
        if (count<=0 || static_cast<std::size_t>(count)>=sizeof(line)-used) return false;
        used+=static_cast<std::size_t>(count);
        PEARL_FINAL_FIELD("requested_duration_seconds",sustained->policy.seconds);
        PEARL_FINAL_FIELD("minimum_accepted",sustained->policy.minimumAccepted);
        PEARL_FINAL_FIELD("active_start_ns",static_cast<uint64_t>(sustained->activeStartNs));
        PEARL_FINAL_FIELD("active_deadline_ns",static_cast<uint64_t>(sustained->deadlineNs));
        PEARL_FINAL_FIELD("stop_admitted_ns",static_cast<uint64_t>(sustained->stopNs));
        PEARL_FINAL_FIELD("drain_complete_ns",static_cast<uint64_t>(sustained->drainNs));
    }
    PEARL_FINAL_FIELD("local_records",localRecords); PEARL_FINAL_FIELD("local_bytes",local.bytes);
    PEARL_FINAL_FIELD("wire_bytes",final.wire.bytes);
    PEARL_FINAL_FIELD("submitted",p.submitted); PEARL_FINAL_FIELD("acknowledged",p.acknowledged);
    PEARL_FINAL_FIELD("accepted",p.accepted); PEARL_FINAL_FIELD("pool_rejected",p.rejected);
    PEARL_FINAL_FIELD("stale",p.stale); PEARL_FINAL_FIELD("unknown",p.unknown);
    PEARL_FINAL_FIELD("pending",p.pending()); PEARL_FINAL_FIELD("accepted_superseded",p.acceptedSuperseded);
    PEARL_FINAL_FIELD("host_invalid",hostInvalid); PEARL_FINAL_FIELD("submit_failed",submitFailed);
    PEARL_FINAL_FIELD("total_work_units",work); PEARL_FINAL_FIELD("total_attempts",attempts);
    PEARL_FINAL_FIELD("submit_attempts",g.submitAttempts); PEARL_FINAL_FIELD("submission_failures",g.submissionFailures);
    PEARL_FINAL_FIELD("possibly_delivered_unknowns",l.possiblyDeliveredUnknowns);
    PEARL_FINAL_FIELD("unacknowledged_unknowns",l.unacknowledgedUnknowns);
    PEARL_FINAL_FIELD("acknowledgements",l.acknowledgements); PEARL_FINAL_FIELD("outcomes",l.outcomes);
    PEARL_FINAL_FIELD("searches_started",g.searchesStarted); PEARL_FINAL_FIELD("searches_completed",g.searchesCompleted);
    PEARL_FINAL_FIELD("searches_failed",g.searchesFailed); PEARL_FINAL_FIELD("attempts_started",g.attemptsStarted);
    PEARL_FINAL_FIELD("attempts_completed",g.attemptsCompleted);
    PEARL_FINAL_FIELD("last_source_event_token",s.lastEventToken); PEARL_FINAL_FIELD("last_operation_token",s.lastOperationToken);
    PEARL_FINAL_FIELD("source_published_events",s.publishedEvents); PEARL_FINAL_FIELD("source_drained_events",s.drainedEvents);
    PEARL_FINAL_FIELD("source_queued_events",s.queued); PEARL_FINAL_FIELD("source_refused_events",s.refusedEvents);
    PEARL_FINAL_FIELD("source_invalid_events",o.sourceEventsInvalid); PEARL_FINAL_FIELD("source_mailbox_fault_bits",o.sourceMailboxFaultBits);
    PEARL_FINAL_FIELD("source_integrity_flags",o.sourceIntegrityFlags); PEARL_FINAL_FIELD("source_fault_events",o.sourceFaultEvents);
    PEARL_FINAL_FIELD("terminal_observed",o.terminalEvidenceObserved); PEARL_FINAL_FIELD("terminal_consumed",o.terminalEvidenceConsumed);
    PEARL_FINAL_FIELD("terminal_dropped",o.terminalEvidenceDropped); PEARL_FINAL_FIELD("terminal_remaining",o.terminalEvidenceCount);
#undef PEARL_FINAL_FIELD
    if (sizeof(line)-used<3) return false;
    line[used++]='}'; line[used++]='\n';
    // Caller owns the one-shot final file. Close must still be attempted on
    // failure, and no partially-written file grants native completion.
    const bool written=writer_.write(line,used)==used;
    const bool flushed=writer_.flush();
    const bool closed=writer_.close();
    return !reentered_ && written && flushed && closed;
}
 private:
    PearlLocalRetirementWriter &writer_;
    bool attempted_=false,reentered_=false;
};
} // namespace om
