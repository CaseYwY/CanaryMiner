// Native local review of immutable owner snapshots only. No files/helpers, source discovery, imports or miner actions.
#pragma once
#include "credit_owner.h"
#include "dashboard.h"
#include <functional>
namespace om {
struct CreditReviewAccess {
    // Only in-memory snapshot/admission callbacks. Real owners must use retryReviewed, not legacy retryUncertain.
    std::function<std::shared_ptr<const CreditOwnerSnapshot>()> snapshot;
    std::function<CreditEnqueueResult(uint64_t)> inspect;
    std::function<CreditEnqueueResult(uint64_t,uint64_t)> retry;
};
enum class CreditReviewResult { None, Close, Quit };
class CreditReviewPanel {
   public:
    bool visible=false,reviewing=false,reveal=false,acknowledged=false;
    size_t offset=0;
    uint64_t submittedTicket=0,submittedGeneration=0;
    std::string message;
    std::shared_ptr<const CreditOwnerSnapshot> current,reviewed;
    void open(std::shared_ptr<const CreditOwnerSnapshot> snapshot={}) {
        visible=true;reviewing=reveal=acknowledged=false;offset=0;submittedTicket=submittedGeneration=0;message.clear();reviewed.reset();current=std::move(snapshot);
    }
    void update(std::shared_ptr<const CreditOwnerSnapshot> snapshot) {
        if(reviewing&&(!snapshot||!reviewed||snapshot->generation!=reviewed->generation||snapshot->revision!=reviewed->revision)) {
            reviewing=false;reviewed.reset();acknowledged=false;offset=0;
            message="Owner changed; review cancelled. Inspect the current original batch again before retry.";
        }
        current=std::move(snapshot);
        if(submittedTicket&&current&&current->generation==submittedGeneration) {
            const auto &r=current->reply;
            if(r&&r->ticket==submittedTicket&&r->generation==submittedGeneration&&current->lastSuccessfulTicket==submittedTicket&&
                current->state==CreditOwnerState::Ready&&!current->writeOutcomeUnknown&&r->applied&&
                (*r->applied==CreditLedgerStatus::Ok||*r->applied==CreditLedgerStatus::Replay)&&r->recovery&&
                r->recovery->state==CreditRecoveryState::Confirmed&&r->recovery->status==CreditLedgerStatus::Ok) {
                message="Reviewed ticket "+std::to_string(submittedTicket)+" confirmed: "+status(*r->applied)+" with persisted receipt. Original times unchanged; not a source cursor or income activation.";
                submittedTicket=submittedGeneration=0;
            } else if(current->state==CreditOwnerState::Faulted||current->state==CreditOwnerState::Stopped) {
                message="Reviewed retry did not establish a confirmed result. Preserve original input; no automatic retry or erasure.";
                submittedTicket=submittedGeneration=0;
            }
        }
    }
    CreditReviewResult key(int key,const CreditReviewAccess &access={}) {
        if(key==3||key=='q'||key=='Q')return CreditReviewResult::Quit;
        if(key==27){if(reviewing){reviewing=false;reviewed.reset();acknowledged=false;offset=0;message="Retry review cancelled; no batch submitted.";}
            else{visible=false;reveal=false;return CreditReviewResult::Close;}}
        else if(key==kKeyUp||key==kKeyPageUp){const size_t step=key==kKeyUp?1:5;offset=offset>step?offset-step:0;}
        else if(key==kKeyDown||key==kKeyPageDown)offset=std::min(size_t(8192),offset+(key==kKeyDown?1:5));
        else if(key=='i'||key=='I'){reveal=!reveal;acknowledged=false;offset=0;message=reveal?"LOCAL PRIVATE values revealed; do not share screenshots or terminal capture.":"Identifiers, amounts and original input hidden.";}
        else if((key=='r'||key=='R')&&!reviewing) {
            if(!eligible(current)||!access.retry)message="Explicit retry unavailable; no prepared input with a ready persistent owner. Preserve files; no destructive repair.";
            else{reviewed=current;reviewing=true;acknowledged=false;offset=0;message="Review ORIGINAL retained input; no edit/rebind/fresh timestamp or automatic replay.";}
        } else if(key==' '&&reviewing) {
            if(reveal)acknowledged=!acknowledged;
            else message="Reveal the private original input with I before acknowledging exact replay.";
        } else if((key=='\r'||key=='\n'||key==kKeyF2)&&reviewing) {
            if(!acknowledged||!reveal)message="No request sent. Reveal with I, review, then Space to acknowledge exact original replay.";
            else if(!eligible(reviewed)||!access.retry)message="Retry unavailable; nothing sent.";
            else {
                const auto result=access.retry(reviewed->generation,reviewed->revision);
                submittedTicket=result.status==CreditEnqueueStatus::Queued?result.ticket:0;
                submittedGeneration=submittedTicket?reviewed->generation:0;
                message=result.status==CreditEnqueueStatus::Queued?"Retry QUEUED as ticket "+std::to_string(result.ticket)+"; NOT a durable acknowledgement, source cursor or income.":admission(result.status);
                reviewing=false;reviewed.reset();acknowledged=false;offset=0;
            }
        } else if((key=='f'||key=='F')&&!reviewing) {
            if(!current||!access.inspect)message="No local collector/owner is active; no store was opened.";
            else if(current->pendingJobs)message="Operation pending; no duplicate inspection queued.";
            else{const auto result=access.inspect(current->generation);message=result.status==CreditEnqueueStatus::Queued?"Inspection QUEUED; no automatic replay and no durable income acknowledgement.":admission(result.status);}
        }
        return CreditReviewResult::None;
    }
    std::string render(int columns,int rows,ColorLevel color)const {
        using namespace dash_detail;const size_t width=static_cast<size_t>(std::max(20,columns)-1);
        if(rows>0&&rows<12)return "Terminal too short.\nResize to 12+ rows.\n[Q/Ctrl+C] Stop [Esc] Back\n";
        const auto footer=wrapText(reviewing?std::string(acknowledged?"[x]":"[ ]")+" EXACT replay | [I] Private values [Space] Acknowledge [Enter/F2] Retry ONCE [Arrows] Scroll [Esc] Cancel [Q/Ctrl+C] Stop":"[I] Private values [R] Review exact retry [F] Inspect [Arrows] Scroll [Esc] Back [Q/Ctrl+C] Stop",width);
        const auto notice=wrapText(message.empty()?"No delete/reset/rebuild/import/automatic replay. No actual-income activation.":message,width);
        const size_t budget=rows>0?static_cast<size_t>(std::max(1,rows-static_cast<int>(footer.size()+notice.size())-1)):10000;
        const auto content=lines();std::vector<std::string> wrapped;
        for(const auto &line:content)for(const auto &part:wrapText(line,width))wrapped.push_back(part);
        const size_t start=std::min(offset,wrapped.size()>budget?wrapped.size()-budget:size_t(0));
        std::string out;for(size_t i=start;i<wrapped.size()&&i<start+budget;++i)out+=paint(wrapped[i],i==start?Tone::Canary:Tone::Plain,color)+"\n";
        for(const auto &line:notice)out+=line+"\n";
        for(const auto &line:footer)out+=line+"\n";
        return out;
    }
    std::vector<std::string> lines()const {
        std::vector<std::string> out={reviewing?"Credit recovery / REVIEW ORIGINAL INPUT":"Credit recovery / local collection status"};
        const auto s=reviewing?reviewed:current;
        if(!s){out.push_back("UNAVAILABLE: no qualified local credit collector/owner is active.");out.push_back("No ledger or journal opened, created, inspected or replayed by this page.");out.push_back("Real-source qualification and production actual income remain unavailable.");return out;}
        out.push_back(s->binding.qualification==CreditQualification::SyntheticFixture?"SYNTHETIC FIXTURE ONLY - not real pool income.":"UNQUALIFIED source - no production income or recovery permission.");
        out.push_back("Owner: "+std::string(ownerName(s->state))+" | data generation "+std::to_string(s->generation)+" | snapshot revision "+std::to_string(s->revision));
        out.push_back("Recovery: "+std::string(recoveryName(s->recoveryState))+" | durable mode "+(s->persistentRecovery?"on":"OFF (volatile compatibility)"));
        out.push_back(meaning(s->recoveryState));
        if(s->writeOutcomeUnknown)out.push_back("UNKNOWN WRITE OUTCOME overrides older receipts; exact retained input still requires explicit reconciliation.");
        out.push_back("Queue tickets: admitted "+std::to_string(s->lastAcceptedTicket)+", completed "+std::to_string(s->lastCompletedTicket)+", successful operation "+std::to_string(s->lastSuccessfulTicket));
        out.push_back("Queue pending "+std::to_string(s->pendingJobs)+" / reserved wire bytes "+std::to_string(s->reservedWireBytes)+" / discarded jobs "+std::to_string(s->droppedJobs));
        out.push_back("Queued/completed/successful operation tickets are NOT durable source cursors or complete income.");
        out.push_back("Transport: "+std::string(transportName(s->transport)));
        if(s->reply){out.push_back("Reply ticket "+std::to_string(s->reply->ticket)+" | ledger open "+status(s->reply->opened)+" | apply "+(s->reply->applied?status(*s->reply->applied):"not attempted"));
            if(s->reply->recovery)out.push_back("Journal/proof status: "+status(s->reply->recovery->status)+" | retained apply receipt "+(s->reply->recovery->applied?status(*s->reply->recovery->applied):"none"));}
        out.push_back("Retained durable entry sequence: "+std::to_string(s->durableSequence)+" (not necessarily committed)");
        if(s->binding.cursor){
            const auto *c=s->reply&&s->reply->cursor?&*s->reply->cursor:nullptr;
            out.push_back("Committed source checkpoint: "+(c?status(c->status):std::string("unavailable"))+(c&&c->status==CreditLedgerStatus::Ok?" | sequence "+std::to_string(c->sequence):""));
            out.push_back("A committed checkpoint is separate from pending recovery; only Ready with no pending job can admit a next page.");
        }
        const CreditBatch *batch=s->uncertainBatch?s->uncertainBatch.get():s->reply&&s->reply->recovery&&s->reply->recovery->batch?&*s->reply->recovery->batch:nullptr;
        if(!reveal)out.push_back("PRIVATE values hidden: provider/scope/IDs/cursors, original times/amounts/records/coverage. I reveals locally.");
        else{
            const auto &b=s->binding;out.push_back("LOCAL PRIVATE review - do not upload screenshots, state bundles or raw input.");
            out.push_back("Provider / contract: "+safe(b.provider)+" / "+safe(b.contractVersion));
            out.push_back("Scope: "+std::string(b.scope==CreditScope::Worker?"worker":"ACCOUNT (may include other machines)")+" | "+safe(b.scopeId)+" / generation "+safe(b.scopeGeneration));
            out.push_back(std::string(b.asset==CreditAsset::PearlMainnetNative?"Pearl mainnet native PRL":"UNSUPPORTED asset")+" | "+
                (b.timeBasis==CreditTimeBasis::PostedUtcMilliseconds?"posted UTC milliseconds":"UNSUPPORTED time basis")+" | "+
                (b.revisionModel==CreditRevisionModel::ReplacementValues?"replacement revisions":"UNSUPPORTED revision model")+" | "+
                (b.amountBasis==CreditAmountBasis::PostedNetOfPoolCharges?"net of pool charges":"UNSUPPORTED amount basis")+" | "+
                (b.scaleDeclared?"declared scale "+std::to_string(b.amountScale):"MISSING declared scale"));
            out.push_back("Declared history start UTC ms: "+std::to_string(b.historyStartMs));
            out.push_back("Durable entry ID: "+safe(s->durableSnapshotId)+" | uncertain ID: "+safe(s->uncertainSnapshotId));
            if(b.cursor){out.push_back("Cursor contract: "+safe(b.cursor->version));out.push_back("Initial cursor: "+safe(b.cursor->initial,512));
                if(s->reply&&s->reply->cursor&&s->reply->cursor->status==CreditLedgerStatus::Ok){const auto &c=*s->reply->cursor;out.push_back("Committed next cursor: "+safe(c.next,512));out.push_back("Committed checkpoint snapshot: "+safe(c.snapshotId)+" | original observed UTC ms "+std::to_string(c.observedAtMs));}}
            if(batch){out.push_back("ORIGINAL retained batch: sequence "+std::to_string(batch->sequence)+" / ID "+safe(batch->snapshotId));
                if(batch->cursor){out.push_back("Original cursor BEFORE: "+safe(batch->cursor->before,512));out.push_back("Original cursor AFTER: "+safe(batch->cursor->after,512));}
                out.push_back("Original source-as-of UTC ms "+std::to_string(batch->sourceAsOfMs)+" / observed UTC ms "+std::to_string(batch->observedAtMs)+" (not refreshed by review/retry)");
                out.push_back("Original records "+std::to_string(batch->records.size())+" / coverage assertions "+std::to_string(batch->coverage.size())+" (empty coverage means unknown, not zero)");
                for(size_t i=0;i<batch->records.size()&&i<512;++i){const auto &r=batch->records[i];out.push_back("Record "+std::to_string(i+1)+": "+safe(r.id)+" revision "+std::to_string(r.revision)+" | "+(r.kind==CreditKind::PostedCredit?"posted credit":"posted adjustment")+" | posted UTC ms "+std::to_string(r.postedAtMs)+" | replacement "+r.amount.text()+" PRL | reference "+safe(r.reference));}
                for(size_t i=0;i<batch->coverage.size()&&i<128;++i){const auto &c=batch->coverage[i];out.push_back("Coverage "+std::to_string(i+1)+": ("+std::to_string(c.fromExclusiveMs)+","+std::to_string(c.throughInclusiveMs)+"] UTC ms");}}
            else out.push_back("Original input unavailable in this snapshot; no replay can be reviewed here.");
        }
        if(reviewing){out.push_back(std::string(acknowledged?"[x]":"[ ]")+" Replay EXACT retained original ONCE; it may already have committed. No values/timestamps/IDs are edited.");out.push_back("Only an Ok/exact Replay plus persisted confirmation/proof can resolve; queue admission is not success.");}
        out.push_back(s->binding.cursor?"No manual cursor edits/deletion/rebuild/income/mining actions. Exact retry may atomically commit facts + cursor; never automatic.":"No deletion, rebuild, cursor advancement, income calculation, mining start/restart or automatic retry.");
        return out;
    }
   private:
    static bool eligible(const std::shared_ptr<const CreditOwnerSnapshot> &s){return s&&s->persistentRecovery&&s->state==CreditOwnerState::NeedsReconciliation&&s->writeOutcomeUnknown&&s->uncertainBatch&&!s->pendingJobs&&s->binding.qualification==CreditQualification::SyntheticFixture;}
    static std::string safe(const std::string &text,size_t maximum=128){std::string s;for(unsigned char c:text){if(s.size()>=maximum){s+="...";break;}s+=c>=32&&c<=126?static_cast<char>(c):'?';}return s.empty()?"none":s;}
    static const char *ownerName(CreditOwnerState s){switch(s){case CreditOwnerState::Stopped:return "Stopped";case CreditOwnerState::Starting:return "Starting";case CreditOwnerState::Ready:return "Ready";case CreditOwnerState::Busy:return "Busy";case CreditOwnerState::NeedsReconciliation:return "NeedsReconciliation";case CreditOwnerState::Faulted:return "Faulted";}return "Unknown";}
    static const char *recoveryName(CreditRecoveryState s){switch(s){case CreditRecoveryState::Disabled:return "Disabled";case CreditRecoveryState::Empty:return "Empty";case CreditRecoveryState::Prepared:return "Prepared";case CreditRecoveryState::Confirmed:return "Confirmed";case CreditRecoveryState::Rejected:return "Rejected";case CreditRecoveryState::Diverged:return "Diverged";case CreditRecoveryState::Unavailable:return "Unavailable";}return "Unknown";}
    static const char *meaning(CreditRecoveryState s){switch(s){case CreditRecoveryState::Disabled:return "No durable protocol; not permission to bypass a pending journal.";case CreditRecoveryState::Empty:return "No retained journal entry; not proof of complete empty source history.";case CreditRecoveryState::Prepared:return "Original intent retained; final write outcome not established. New batches blocked.";case CreditRecoveryState::Confirmed:return "Retained receipt needs current latest-ledger proof; no new replay by opening this page.";case CreditRecoveryState::Rejected:return "New batch definitely rejected; preserve original. Corrected source input needs separate review.";case CreditRecoveryState::Diverged:return "Receipt and current ledger disagree; no recreate/delete/automatic repair. New batches blocked.";case CreditRecoveryState::Unavailable:return "Journal/store/binding unavailable; preserve files, never rebuild as zero.";}return "Unknown state; no action permission.";}
    static std::string status(CreditLedgerStatus s){switch(s){case CreditLedgerStatus::Ok:return "Ok";case CreditLedgerStatus::Replay:return "Replay";case CreditLedgerStatus::StaleBatch:return "StaleBatch";case CreditLedgerStatus::Invalid:return "Invalid";case CreditLedgerStatus::Unsupported:return "Unsupported";case CreditLedgerStatus::Attribution:return "Attribution";case CreditLedgerStatus::Conflict:return "Conflict";case CreditLedgerStatus::SequenceGap:return "SequenceGap";case CreditLedgerStatus::Clock:return "Clock";case CreditLedgerStatus::Capacity:return "Capacity";case CreditLedgerStatus::Storage:return "Storage";}return "Unknown";}
    static const char *transportName(LocalTaskStatus s){switch(s){case LocalTaskStatus::Ok:return "Ok";case LocalTaskStatus::Invalid:return "Invalid";case LocalTaskStatus::Unavailable:return "Unavailable";case LocalTaskStatus::Busy:return "Busy";case LocalTaskStatus::Cancelled:return "Cancelled";case LocalTaskStatus::Timeout:return "Timeout";case LocalTaskStatus::Unreaped:return "Unreaped";}return "Unknown";}
    static std::string admission(CreditEnqueueStatus s){switch(s){case CreditEnqueueStatus::Queued:return "Queued; not a durable acknowledgement.";case CreditEnqueueStatus::NotReady:return "Owner not ready; no retry admitted. Preserve original input.";case CreditEnqueueStatus::StaleGeneration:return "Data generation changed; reopen review. Nothing replayed.";case CreditEnqueueStatus::Full:return "Queue full; no retry admitted. Original remains pending.";case CreditEnqueueStatus::Invalid:return "Invalid/unsupported recovery request; original remains pending.";case CreditEnqueueStatus::NeedsReconciliation:return "Reconciliation required; no new submission permitted.";case CreditEnqueueStatus::ReviewChanged:return "Owner revision changed; review the current original again. Nothing replayed.";}return "No request admitted.";}
};
} // namespace om
