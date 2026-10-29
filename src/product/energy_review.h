// Native private review of a capture snapshot. All callbacks are memory-only admission; no synchronous files/helpers.
#pragma once
#include "energy_capture.h"
#include "energy_authorization.h"
#include "dashboard.h"

namespace om {
struct EnergyReviewAccess {
    std::function<std::shared_ptr<const EnergyCaptureSnapshot>()> snapshot;
    std::function<EnergyHistoryAdmission(uint64_t,uint64_t)> inspect,retry;
};
enum class EnergyReviewResult { None,Close,Quit };
class EnergyReviewPanel {
   public:
    bool visible=false,reveal=false,reviewing=false,acknowledged=false;
    size_t offset=0;
    uint64_t submittedTicket=0,submittedGeneration=0;
    std::string message;
    std::shared_ptr<const EnergyCaptureSnapshot> current,reviewed;
    void open(std::shared_ptr<const EnergyCaptureSnapshot> s={}){
        visible=true;reveal=reviewing=acknowledged=false;offset=0;submittedTicket=submittedGeneration=0;message.clear();reviewed.reset();current=std::move(s);
    }
    void update(std::shared_ptr<const EnergyCaptureSnapshot> s){
        if(reviewing&&(!s||!s->owner||!reviewed||!reviewed->owner||s->generation!=reviewed->generation||s->owner->revision!=reviewed->owner->revision)){
            reviewing=acknowledged=false;reviewed.reset();offset=0;message="Owner changed; review cancelled. Inspect the current original before retry.";
        }
        current=std::move(s);
        if(submittedTicket&&current&&current->generation==submittedGeneration&&current->owner){
            const auto confirmed=current->reconciled&&current->reconciled->reply&&current->reconciled->reply->ticket==submittedTicket?current->reconciled:current->owner;
            const auto &o=*confirmed;
            if(o.state==EnergyHistoryOwnerState::Ready&&!o.uncertainBatch&&o.reply&&o.reply->ticket==submittedTicket&&o.reply->recovery&&
                o.reply->recovery->state==EnergyRecoveryState::Confirmed&&o.reply->recovery->status==EnergyHistoryStatus::Ok&&o.reply->applied&&
                (*o.reply->applied==EnergyHistoryStatus::Ok||*o.reply->applied==EnergyHistoryStatus::Replay)){
                message="Exact original confirmed with persisted receipt. Original observation times retained.";submittedTicket=submittedGeneration=0;
            }else if(current->owner->state==EnergyHistoryOwnerState::Faulted||current->owner->state==EnergyHistoryOwnerState::Stopped){
                message="Retry outcome not confirmed; preserve original input and stores.";submittedTicket=submittedGeneration=0;
            }
        }
    }
    EnergyReviewResult key(int key,const EnergyReviewAccess &access={}){
        if(key==3||key=='q'||key=='Q')return EnergyReviewResult::Quit;
        if(key==27){if(reviewing){reviewing=acknowledged=false;reviewed.reset();offset=0;message="Review cancelled; no retry sent.";}
            else{visible=reveal=false;return EnergyReviewResult::Close;}}
        else if(key==kKeyUp||key==kKeyPageUp){const size_t step=key==kKeyUp?1:5;offset=offset>step?offset-step:0;}
        else if(key==kKeyDown||key==kKeyPageDown)offset=(std::min)(size_t(8192),offset+(key==kKeyDown?1:5));
        else if(key=='i'||key=='I'){reveal=!reveal;acknowledged=false;offset=0;message=reveal?"Private local values revealed; do not share terminal captures.":"Private identity, times and original readings hidden.";}
        else if((key=='r'||key=='R')&&!reviewing){
            if(!eligible(current)||!access.retry)message="No reviewable pending original. Preserve stores; no automatic repair.";
            else{reviewed=current;reviewing=true;acknowledged=false;offset=0;message="Review exact retained original; retry does not edit observations.";}
        }else if(key==' '&&reviewing){if(reveal)acknowledged=!acknowledged;else message="I reveals the original input before acknowledgement.";}
        else if((key=='\r'||key=='\n'||key==kKeyF2)&&reviewing){
            if(!reveal||!acknowledged)message="Reveal with I, review, then Space to acknowledge exact replay.";
            else if(!eligible(reviewed)||!access.retry)message="Retry unavailable; nothing sent.";
            else{const auto &o=*reviewed->owner;const auto result=access.retry(reviewed->generation,o.revision);
                if(result==EnergyHistoryAdmission::Queued){submittedTicket=o.lastTicket+1;submittedGeneration=reviewed->generation;}
                message=admission(result);reviewing=acknowledged=false;reviewed.reset();offset=0;}
        }else if((key=='f'||key=='F')&&!reviewing){
            if(!current||!current->owner||!access.inspect)message="No active durable collector; no store opened.";
            else message=admission(access.inspect(current->generation,current->owner->revision));
        }
        return EnergyReviewResult::None;
    }
    std::vector<std::string> lines()const{
        std::vector<std::string> out={reviewing?"Energy history / REVIEW ORIGINAL INPUT":"Energy history / durable capture status"};
        const auto s=reviewing?reviewed:current;
        if(!s){out.push_back("UNAVAILABLE: no authorized durable capture owner is active.");out.push_back("Session energy collection alone does not authorize disk retention.");return out;}
        out.push_back(s->binding.evidence.origin==PowerOrigin::SyntheticFixture?"SYNTHETIC FIXTURE ONLY - not physical energy.":"GPU BOARD telemetry proxy; not whole-PC or incremental energy.");
        out.push_back("Capture: "+std::string(state(s->state))+" | generation "+std::to_string(s->generation));
        if(s->leaseState!=EnergyLeaseState::Unmanaged){out.push_back("Lifetime lease: "+std::string(lease(s->leaseState))+"; writes require the current fence.");
            if(s->provisioningOutcomeUnknown)out.push_back("Session reservation outcome UNKNOWN; no old identity is adopted or automatically retried.");}
        if(s->provisioningOutcomeUnknown&&s->provisionRequest){
            if(!reveal)out.push_back("PRIVATE original reservation request hidden. I reveals locally.");
            else{const auto &r=*s->provisionRequest;
                out.push_back("ORIGINAL reservation: Provision | generation "+std::to_string(r.generation)+" | ticket "+std::to_string(r.ticket));
                out.push_back("Original UTC ms "+std::to_string(r.nowUtcMs)+" | expected snapshot "+safe(r.expected));
                const auto path=r.file.u8string();out.push_back("Authorization path fragments (non-ASCII shown as ?):");
                for(size_t n=0;n<path.size();n+=96)out.push_back(safe(path.substr(n,96)));
                for(const auto &id:r.devices)out.push_back("Selected UUID "+safe(id));}}
        out.push_back("Volatile buffered readings "+std::to_string(s->buffered)+" | dropped/discarded "+std::to_string(s->dropped)+" | cadence skips "+std::to_string(s->selectedOut));
        out.push_back("Continuity "+std::to_string(s->continuity)+"; buffered input is not durable. No final power hold.");
        if(!s->owner){out.push_back("Owner unavailable; no committed quantity inferred.");return out;}
        const auto &o=*s->owner;
        out.push_back("Owner revision "+std::to_string(o.revision)+" | ticket "+std::to_string(o.lastTicket)+(o.pending?" pending":" idle"));
        out.push_back("Recovery: "+std::string(recovery(o.recoveryState))+(o.creationOutcomeUnknown?" | creation outcome UNKNOWN":""));
        if(o.uncertainBatch)out.push_back("UNKNOWN original outcome blocks new sampling until explicit exact reconciliation.");
        if(o.reply){const auto &r=*o.reply;const auto &c=r.checkpoint;
            out.push_back("Store open: "+std::string(status(r.opened))+" | apply: "+(r.applied?status(*r.applied):"not attempted"));
            if(c.status==EnergyHistoryStatus::Ok){out.push_back("Committed sequence "+std::to_string(c.sequence)+" | readings "+std::to_string(c.readingCount));
                out.push_back("Compacted sequence "+std::to_string(c.compactedSequence)+"; earlier raw originals expired, exact aggregates retained.");}
            else out.push_back("Committed checkpoint unavailable; not an empty history.");
        }
        const auto *b=o.uncertainBatch?o.uncertainBatch.get():o.reply&&o.reply->recovery&&o.reply->recovery->batch?&*o.reply->recovery->batch:nullptr;
        if(!reveal)out.push_back("PRIVATE identity, timestamps and original power readings hidden. I reveals locally.");
        else{
            out.push_back("LOCAL PRIVATE values - exclude from screenshots, logs and exports.");
            out.push_back("Scope "+safe(s->binding.evidence.scope)+" | adapter "+safe(s->binding.evidence.adapter));
            out.push_back("History lower bound UTC ms "+std::to_string(s->binding.historyBeginUtcMs));
            for(size_t n=0;n<s->binding.channels.size()&&n<64;++n)out.push_back("Channel "+std::to_string(n)+": "+safe(s->binding.channels[n]));
            if(b){out.push_back("ORIGINAL batch "+safe(b->id)+" | sequence "+std::to_string(b->sequence)+" | session "+safe(b->sessionId));
                out.push_back("Original observed UTC ms "+std::to_string(b->observedAtMs)+" (never renewed by retry)");
                if(b->start)out.push_back("Start "+safe(b->start->id)+" | UTC "+std::to_string(b->start->anchorUtcMs)+" | mono "+std::to_string(b->start->anchorMonoMs)+" | gap "+std::to_string(b->start->maximumGapMs)+" ms");
                for(size_t n=0;n<b->readings.size()&&n<128;++n){const auto &r=b->readings[n];out.push_back("Reading "+std::to_string(n+1)+": channel "+std::to_string(r.channel)+" | UTC "+std::to_string(r.utcMs)+" | mono "+std::to_string(r.monotonicMs)+" | continuity "+std::to_string(r.continuity)+" | mW "+(r.milliwatts?std::to_string(*r.milliwatts):"MISSING"));}
                for(size_t n=0;n<b->windows.size()&&n<32;++n){const auto &w=b->windows[n];out.push_back("Window "+safe(w.registerId)+" / "+safe(w.periodId)+" / "+safe(w.ruleVersion)+" ["+std::to_string(w.beginUtcMs)+","+std::to_string(w.endUtcMs)+") UTC ms | "+(w.fixedStandardTime?"fixed standard":"UTC")+" offset "+std::to_string(w.offsetSeconds));}
            }else out.push_back("No retained original in this snapshot.");
        }
        if(reviewing)out.push_back(std::string(acknowledged?"[x]":"[ ]")+" Replay EXACT original once; may already be committed. Queue admission is not confirmation.");
        out.push_back("No new mining session, tariff settlement, income or completeness inferred from this review.");
        return out;
    }
    std::string render(int columns,int rows,ColorLevel color)const{
        using namespace dash_detail;const size_t width=static_cast<size_t>((std::max)(20,columns)-1);
        if(rows>0&&rows<12)return "Terminal too short.\nResize to 12+ rows.\n[Q/Ctrl+C] Stop [Esc] Back\n";
        auto footer=wrapText(reviewing?"[I] Reveal [Space] Ack [Enter] Retry [Arrows] Scroll [Esc] Cancel [Q/Ctrl+C] Stop":"[I] Reveal [R] Review [F] Inspect [Arrows] Scroll [Esc] Back [Q/Ctrl+C] Stop",width);
        auto note=wrapText(message.empty()?"Retention consent is separate from session collection.":message,width);if(note.size()>2)note.resize(2);
        const auto budget=rows>0?static_cast<size_t>((std::max)(1,rows-static_cast<int>(footer.size()+note.size())-1)):size_t(10000);
        std::vector<std::string> content;for(const auto &line:lines())for(const auto &part:wrapText(line,width))content.push_back(part);
        const auto start=(std::min)(offset,content.size()>budget?content.size()-budget:size_t(0));std::string out;
        for(size_t n=start;n<content.size()&&n<start+budget;++n)out+=paint(content[n],n==start?Tone::Canary:Tone::Plain,color)+"\n";
        for(const auto &line:note)out+=line+"\n";
        for(const auto &line:footer)out+=line+"\n";
        return out;
    }
   private:
    static bool eligible(const std::shared_ptr<const EnergyCaptureSnapshot> &s){return s&&s->state==EnergyCaptureState::NeedsReview&&s->owner&&s->owner->state==EnergyHistoryOwnerState::NeedsReview&&s->owner->uncertainBatch&&!s->owner->pending;}
    static std::string safe(const std::string &s){std::string out;for(unsigned char c:s){if(out.size()==128)break;out+=c>=32&&c<=126?static_cast<char>(c):'?';}return out;}
    static const char *state(EnergyCaptureState s){switch(s){case EnergyCaptureState::Stopped:return "Stopped";case EnergyCaptureState::Starting:return "Starting";case EnergyCaptureState::Collecting:return "Collecting";case EnergyCaptureState::NeedsReview:return "NeedsReview";case EnergyCaptureState::Faulted:return "Faulted";case EnergyCaptureState::ClockFault:return "ClockFault";}return "Unknown";}
    static const char *lease(EnergyLeaseState s){switch(s){case EnergyLeaseState::Unmanaged:return "Unmanaged";case EnergyLeaseState::Starting:return "Starting";case EnergyLeaseState::Held:return "Held";case EnergyLeaseState::Disabled:return "Disabled";case EnergyLeaseState::Lost:return "Lost";case EnergyLeaseState::Stopped:return "Stopped";}return "Unknown";}
    static const char *recovery(EnergyRecoveryState s){switch(s){case EnergyRecoveryState::Disabled:return "Disabled";case EnergyRecoveryState::Empty:return "Empty";case EnergyRecoveryState::Prepared:return "Prepared";case EnergyRecoveryState::Confirmed:return "Confirmed";case EnergyRecoveryState::Rejected:return "Rejected";case EnergyRecoveryState::Diverged:return "Diverged";case EnergyRecoveryState::Unavailable:return "Unavailable";}return "Unknown";}
    static const char *status(EnergyHistoryStatus s){switch(s){case EnergyHistoryStatus::Ok:return "Ok";case EnergyHistoryStatus::Replay:return "Replay";case EnergyHistoryStatus::Invalid:return "Invalid";case EnergyHistoryStatus::Attribution:return "Attribution";case EnergyHistoryStatus::Conflict:return "Conflict";case EnergyHistoryStatus::SequenceGap:return "SequenceGap";case EnergyHistoryStatus::Clock:return "Clock";case EnergyHistoryStatus::Capacity:return "Capacity";case EnergyHistoryStatus::Storage:return "Storage";case EnergyHistoryStatus::DetailExpired:return "DetailExpired";}return "Unknown";}
    static const char *admission(EnergyHistoryAdmission s){switch(s){case EnergyHistoryAdmission::Queued:return "QUEUED once; not a durable acknowledgement.";case EnergyHistoryAdmission::NotReady:return "Owner not ready or observations buffered; nothing queued.";case EnergyHistoryAdmission::StaleGeneration:return "Generation changed; reopen review.";case EnergyHistoryAdmission::ReviewChanged:return "Owner revision changed; review current original again.";case EnergyHistoryAdmission::Invalid:return "Invalid request; original preserved.";}return "Nothing queued.";}
};
} // namespace om
