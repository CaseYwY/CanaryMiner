// Native immutable-snapshot review. Callbacks only admit bounded background work; never perform synchronous I/O.
#pragma once
#include "billing_candidate_document.h"
#include "dashboard.h"
namespace om {
struct BillingReviewAccess {
    std::function<std::shared_ptr<const BillingOwnerSnapshot>()> snapshot;
    std::function<BillingOwnerAdmission(uint64_t,uint64_t)> inspect,create,commit,retry,reconcile;
    std::function<void()> requestStop; // memory-only revocation; caller owns lifetime/join
    std::function<std::shared_ptr<const BillingComputeSnapshot>()> candidateSnapshot;
    std::function<BillingComputeAdmission(uint64_t,uint64_t)> recompute;
    std::function<BillingOwnerAdmission(uint64_t,uint64_t,uint64_t,uint64_t)> submit; // compute generation/revision, monetary generation/revision
};
enum class BillingReviewResult { None,Close,Quit };
class BillingReviewPanel {
   public:
    bool visible=false,reveal=false,reviewing=false,acknowledged=false,candidateMode=false;
    mutable size_t offset=0; // render clamps to the actual wrapped document, including its final row
    uint64_t submittedTicket=0,submittedGeneration=0;
    BillingOwnerAction selected=BillingOwnerAction::Refresh;
    std::string message;
    std::shared_ptr<const BillingOwnerSnapshot> current,reviewed;
    std::shared_ptr<const BillingComputeSnapshot> computed,reviewedCompute;
    void open(std::shared_ptr<const BillingOwnerSnapshot> s={}){
        visible=true;reveal=reviewing=acknowledged=stopped_=candidateMode=false;offset=0;submittedTicket=submittedGeneration=0;
        computed.reset();reviewedCompute.reset();recomputeSent_.reset();
        message.clear();reviewed.reset();sent_.reset();current=std::move(s);clearDocument();
    }
    void updateCandidate(std::shared_ptr<const BillingComputeSnapshot> s){
        if(computed==s)return;
        if(reviewing&&selected==BillingOwnerAction::Submit&&s!=reviewedCompute){cancelReview();message="Candidate changed; review cancelled. Review the new candidate.";}
        computed=std::move(s);clearDocument();
    }
    void sync(const BillingReviewAccess &a){if(a.snapshot)update(a.snapshot());if(a.candidateSnapshot)updateCandidate(a.candidateSnapshot());}
    void update(std::shared_ptr<const BillingOwnerSnapshot> s){
        if(current==s)return;
        if(reviewing&&s!=reviewed){cancelReview();message="Owner changed; review cancelled. Inspect and review again.";}
        current=std::move(s);clearDocument();
        if(!submittedTicket)return;
        if(!current||current->generation!=submittedGeneration||terminal(*current)){
            message="Operation not acknowledged here; preserve original input.";submittedTicket=submittedGeneration=0;
        }else if(current->completedTicket==submittedTicket&&!current->pending){
            if(current->action==BillingOwnerAction::Refresh)message=current->reviewable?"Inspection complete; current read available.":"Inspection unavailable; earlier read stays historical.";
            else if(current->actionProof)message="Historical action proof received. F inspects current facts.";
            else if(current->uncertain)message="Outcome UNKNOWN; preserve exact input. F inspects.";
            else message="No monetary confirmation. F inspects current facts.";
            submittedTicket=submittedGeneration=0;
        }
    }
    BillingReviewResult key(int key,const BillingReviewAccess &access={}){
        if(key==3||key=='q'||key=='Q'){
            if(stopped_)return BillingReviewResult::Quit;
            stopped_=true;cancelReview();reveal=false;clearDocument();if(access.requestStop)access.requestStop();return BillingReviewResult::Quit;
        }
        if(!visible||stopped_)return BillingReviewResult::None;
        sync(access);
        if(key==27){if(reviewing){cancelReview();message="Review cancelled; nothing sent.";}
            else{visible=reveal=false;current.reset();computed.reset();sent_.reset();recomputeSent_.reset();clearDocument();return BillingReviewResult::Close;}}
        else if(key==kKeyUp||key==kKeyPageUp){const size_t step=key==kKeyUp?1:5;offset=(std::min)(offset,maxOffset_);offset=offset>step?offset-step:0;}
        else if(key==kKeyDown||key==kKeyPageDown){const size_t step=key==kKeyDown?1:5;offset=(std::min)(offset,maxOffset_);offset=offset>SIZE_MAX-step?SIZE_MAX:offset+step;}
        else if(key=='t'||key=='T')offset=0;
        else if(key=='b'||key=='B')offset=SIZE_MAX;
        else if(key=='i'||key=='I'){reveal=!reveal;acknowledged=false;offset=0;clearDocument();message=reveal?"Private values revealed locally.":"Private values hidden.";}
        else if((key=='p'||key=='P')&&!reviewing){candidateMode=!candidateMode;offset=0;clearDocument();message=candidateMode?"Candidate preview; amounts remain hypothetical.":"Monetary originals and recovery.";}
        else if((key=='g'||key=='G')&&candidateMode&&!reviewing){
            if(!computed||computed==recomputeSent_||computed->pending||!access.recompute)message="Recompute unavailable; nothing queued.";
            else{const auto result=access.recompute(computed->generation,computed->revision);
                if(result==BillingComputeAdmission::Queued){recomputeSent_=computed;message="Readonly recompute queued; no monetary write.";}
                else message="Recompute not admitted; inspect the latest state.";}
        }else if((key=='v'||key=='V')&&candidateMode&&!reviewing){
            selected=BillingOwnerAction::Submit;
            if(!eligible(selected)||!callback(access,selected))message="Candidate unavailable or historical; nothing sent.";
            else{reviewed=current;reviewedCompute=computed;reviewing=true;acknowledged=false;offset=0;clearDocument();message="Review the full candidate and hypothetical amounts before acknowledgement.";}
        }
        else if((key=='f'||key=='F')&&!reviewing){
            if(!idle()||!access.inspect)message="Inspection unavailable; nothing queued.";
            else admit(access.inspect,BillingOwnerAction::Refresh);
        }else if(!candidateMode&&!reviewing&&(key=='n'||key=='N'||key=='c'||key=='C'||key=='r'||key=='R'||key=='e'||key=='E')){
            selected=key=='n'||key=='N'?BillingOwnerAction::CreateNew:key=='c'||key=='C'?BillingOwnerAction::Commit:key=='r'||key=='R'?BillingOwnerAction::Retry:BillingOwnerAction::Reconcile;
            if(!eligible(selected)||!callback(access,selected))message="Action unavailable for this read; nothing sent.";
            else{reviewed=current;reviewing=true;acknowledged=false;offset=0;clearDocument();message="Reveal and review the complete original before acknowledgement.";}
        }else if(key==' '&&reviewing){
            if(!reveal)message="I reveals the original before acknowledgement.";
            else if(!completeDocument())message="Incomplete review document; action blocked.";
            else{acknowledged=!acknowledged;message=acknowledged?"Exact action acknowledged; Enter sends once.":"Acknowledgement cleared.";}
        }else if((key=='\r'||key=='\n'||key==kKeyF2)&&reviewing){
            if(!reveal||!acknowledged)message="Reveal, review, then Space to acknowledge. Nothing sent.";
            else if(!completeDocument()||!eligible(selected)||!callback(access,selected))message="Review no longer complete or available; nothing sent.";
            else{const auto action=selected;const auto call=callback(access,action);cancelReview();admit(call,action);}
        }
        return BillingReviewResult::None;
    }
    const std::vector<std::string> &lines()const{
        if(!contentDirty_)return content_;
        content_.clear();rowEnds_.clear();layoutWidth_=0;
        auto add=[&](const std::string &s){content_.push_back(s);};
        if(!current){add("UNAVAILABLE: no approved billing owner is active.");add("Opening this page does not open or create a store.");contentDirty_=false;return content_;}
        const auto &s=*current;
        add("SYNTHETIC BILLING ONLY - no actual energy bill or income.");
        add(std::string("Owner: ")+state(s.state)+" | "+(s.pending?"pending":"idle"));
        add(std::string("Last operation status: ")+status(s.status));
        add(s.reviewable?"CURRENT READ: audited middle observation; not a cross-store transaction.":"RETAINED EARLIER READ: explicit inspection required for another action.");
        if(s.view&&s.view->monetary)add(std::string("Current monetary status: ")+(s.reviewable?status(s.view->monetary->status):"not freshly inspected"));
        if(s.view&&s.view->stage){const auto &stage=*s.view->stage;
            add(std::string("Stage: ")+(!stage.present?"absent":stage.phase==BillingInputPhase::Receiving?"Receiving":stage.phase==BillingInputPhase::Issued?"Issued":stage.phase==BillingInputPhase::Complete?"Complete":"unavailable"));
            if(stage.replacementPending)add("Temporary replacement UNKNOWN; preserve both files.");
            if(stage.outcome)add("Stored stage outcome is HISTORICAL, not current monetary facts.");}
        if(s.actionProof)add("HISTORICAL ACTION PROOF: separate from a fresh read.");
        if(s.uncertain)add("UNKNOWN HANDOFF OUTCOME: exact input retained; no automatic replay.");
        if(candidateMode){
            add("CANDIDATE PREVIEW / HYPOTHETICAL ONLY - not current money or income.");
            add(billingCandidateMatches(current,computed)?"Candidate: REVIEWABLE against the current delivered monetary version.":computed&&computed->candidate?"Candidate: HISTORICAL / cannot submit; earlier original retained.":"Candidate: UNAVAILABLE / no zero inferred.");
            if(computed){add(std::string("Compute: ")+computeState(computed->state));add(std::string("Last calculation status: ")+status(computed->status));
                if(computed->sourceCoverage)add(std::string("Last source coverage: ")+(*computed->sourceCoverage==EnergyHistoryCoverage::Complete?"Complete":*computed->sourceCoverage==EnergyHistoryCoverage::Partial?"Partial":"Unavailable"));}
        }
        if(reviewing){add(std::string("Review action: ")+actionName(selected));add(effect(selected));}
        else if(candidateMode){add("[V] Review exact candidate Submit  [G] Readonly recompute");add("[F] Inspect money  [P] Monetary originals / recovery");}
        else{add("[N] Review CreateNew  [C] Review complete staged Commit");add("[R] Review exact Retry  [E] Review stage Reconcile");add("[F] Inspect is readonly. Unavailable actions cannot be sent.");}
        if(!reveal)add("PRIVATE values hidden. I reveals complete binding and originals locally.");
        else{ensureDocument();content_.insert(content_.end(),document_.lines.begin(),document_.lines.end());
            if(!document_.complete)add("INCOMPLETE REVIEW DOCUMENT - action blocked; original retained.");}
        contentDirty_=false;return content_;
    }
    bool completeDocument()const{ensureDocument();return document_.complete;}
    std::string render(int columns,int rows,ColorLevel color)const{
        using namespace dash_detail;const size_t width=columns>0?static_cast<size_t>((std::max)(1,columns-1)):size_t(79);
        if((columns>0&&columns<32)||(rows>0&&rows<12)){
            std::string out;const auto hint=wrapText("Resize: 32x12 minimum. Q/^C Stop; Esc Back.",width);
            for(size_t i=0;i<hint.size()&&(rows<=0||i<static_cast<size_t>(rows));++i)out+=hint[i]+"\n";
            return out;
        }
        const auto footer=wrapText(reviewing?"[I] Reveal [Space] Ack [Enter] Send [T/B] Top/End [Arrows/Pg] Scroll [Esc] Cancel [Q/^C] Stop":candidateMode?
            "[I] Reveal [V] Review Submit [G] Recompute [F] Inspect [P] Recovery [T/B] Top/End [Arrows] Scroll [Esc] Back [Q/^C] Stop":
            "[I] Reveal [F] Inspect [N/C/R/E] Review [P] Candidate [T/B] Top/End [Arrows] Scroll [Esc] Back [Q/^C] Stop",width);
        auto note=wrapText(message.empty()?"Explicit review only; no automatic replay.":message,width);if(note.size()>3)note.resize(3);
        const size_t budget=rows>0?static_cast<size_t>((std::max)(1,rows-static_cast<int>(footer.size()+note.size())-2)):size_t(1000);
        const bool keepEnd=layoutWidth_&&(layoutWidth_!=width||viewBudget_!=budget)&&maxOffset_>0&&offset==maxOffset_;
        const auto &content=lines();layout(width);const auto total=rowEnds_.empty()?0:rowEnds_.back();
        const auto start=(std::min)(keepEnd?SIZE_MAX:offset,total>budget?total-budget:0);viewBudget_=budget;
        maxOffset_=total>budget?total-budget:0;offset=start;
        const auto title=reviewing?std::string("Billing / Review ")+actionName(selected)+(acknowledged?" [x]":" [ ]"):candidateMode?"Billing / candidate preview":"Billing / recovery status";
        std::string out=paint(clip(title,width),Tone::Canary,color)+"\n";
        out+=clip("Rows "+std::to_string(total?start+1:0)+"-"+std::to_string((std::min)(total,start+budget))+"/"+std::to_string(total),width)+"\n";
        for(size_t row=start;row<total&&row<start+budget;++row){const auto it=std::upper_bound(rowEnds_.begin(),rowEnds_.end(),row);const auto n=static_cast<size_t>(it-rowEnds_.begin());
            const auto part=row-(n?rowEnds_[n-1]:0);out+=content[n].substr(part*width,width)+"\n";}
        for(const auto &line:note)out+=line+"\n";
        for(const auto &line:footer)out+=line+"\n";
        return out;
    }
   private:
    bool stopped_=false;
    std::shared_ptr<const BillingOwnerSnapshot> sent_;
    std::shared_ptr<const BillingComputeSnapshot> recomputeSent_;
    mutable bool documentReady_=false,contentDirty_=true;
    mutable billing_review_detail::Document document_;
    mutable std::vector<std::string> content_;
    mutable std::vector<size_t> rowEnds_;
    mutable size_t layoutWidth_=0;
    mutable size_t maxOffset_=SIZE_MAX;
    mutable size_t viewBudget_=0;
    void clearDocument(){documentReady_=false;contentDirty_=true;document_={};content_.clear();rowEnds_.clear();layoutWidth_=0;}
    void cancelReview(){reviewing=acknowledged=false;reviewed.reset();reviewedCompute.reset();offset=0;clearDocument();}
    void ensureDocument()const{
        if(documentReady_)return;
        document_={};
        try{if(candidateMode){if(computed)document_=billing_review_detail::candidateDocument(*computed);else document_.complete=false;}
            else if(current)document_=billing_review_detail::document(*current);else document_.complete=false;}
        catch(...){document_={};document_.complete=false;}
        documentReady_=true;
    }
    void layout(size_t width)const{
        if(layoutWidth_==width)return;
        rowEnds_.clear();size_t count=0;
        for(const auto &line:content_){count+=(std::max)(size_t(1),(line.size()+width-1)/width);rowEnds_.push_back(count);}
        layoutWidth_=width;
    }
    static bool terminal(const BillingOwnerSnapshot &s){return s.state==BillingOwnerState::Stopped||s.state==BillingOwnerState::Faulted||s.state==BillingOwnerState::ClockFault;}
    bool idle()const{return !stopped_&&current&&current!=sent_&&!terminal(*current)&&!current->pending&&
        (current->state==BillingOwnerState::Ready||current->state==BillingOwnerState::NeedsReview)&&current->generation&&current->revision;}
    bool eligible(BillingOwnerAction a)const{
        if(!idle()||!current->reviewable||!current->context||!current->view||!current->view->stable||!current->view->stage||!current->view->monetary)return false;
        const auto &s=*current->view->stage;const auto &m=*current->view->monetary;
        if(s.status!=BillingStatus::Ok||s.replacementPending)return false;
        switch(a){
            case BillingOwnerAction::Submit:return billingCandidateMatches(current,computed);
            case BillingOwnerAction::CreateNew:return !s.present&&!current->uncertain&&m.status==BillingStatus::Storage;
            case BillingOwnerAction::Commit:return s.present&&s.phase==BillingInputPhase::Receiving&&s.manifest.bindingBytes<=1024*1024&&s.manifest.chunkBytes<=128*1024&&
                s.received==s.manifest.bindingBytes+s.manifest.chunkBytes&&(s.manifest.mode==BillingInputMode::CreateNew||s.original.has_value());
            case BillingOwnerAction::Retry:return m.status==BillingStatus::Ok&&m.original&&(m.state==BillingRecoveryState::Prepared||m.state==BillingRecoveryState::Confirmed);
            case BillingOwnerAction::Reconcile:return s.present&&s.phase==BillingInputPhase::Issued&&s.manifest.mode==BillingInputMode::Submit&&m.status==BillingStatus::Ok;
            default:return false;
        }
    }
    std::function<BillingOwnerAdmission(uint64_t,uint64_t)> callback(const BillingReviewAccess &a,BillingOwnerAction action)const{
        if(action==BillingOwnerAction::Submit){if(!a.submit||!computed)return {};const auto call=a.submit;const auto g=computed->generation,r=computed->revision;
            return [call,g,r](uint64_t mg,uint64_t mr){return call(g,r,mg,mr);};}
        switch(action){case BillingOwnerAction::CreateNew:return a.create;case BillingOwnerAction::Commit:return a.commit;case BillingOwnerAction::Retry:return a.retry;case BillingOwnerAction::Reconcile:return a.reconcile;default:return {};}
    }
    void admit(const std::function<BillingOwnerAdmission(uint64_t,uint64_t)> &call,BillingOwnerAction action){
        const auto result=call(current->generation,current->revision);
        if(result==BillingOwnerAdmission::Queued){sent_=current;submittedTicket=current->lastTicket+1;submittedGeneration=current->generation;selected=action;}
        message=admission(result);offset=0;contentDirty_=true;
    }
    static const char *actionName(BillingOwnerAction a){switch(a){case BillingOwnerAction::Refresh:return "Inspect";case BillingOwnerAction::CreateNew:return "CreateNew";case BillingOwnerAction::Submit:return "Submit";case BillingOwnerAction::Commit:return "Commit";case BillingOwnerAction::Retry:return "Retry";case BillingOwnerAction::Reconcile:return "Reconcile";}return "Unknown";}
    static const char *effect(BillingOwnerAction a){switch(a){
        case BillingOwnerAction::Submit:return "Submit this EXACT candidate once using both reviewed versions. Preview stays hypothetical until independent monetary confirmation; times and receipt are not renewed.";
        case BillingOwnerAction::CreateNew:return "Create an EMPTY monetary pair with this exact binding. Storage status is not proven absence; strict creation may refuse existing files.";
        case BillingOwnerAction::Commit:return "Commit the complete Receiving original once. This may create a pair or apply money; original times and receipts stay frozen.";
        case BillingOwnerAction::Retry:return "Retry the EXACT monetary original once. It may already be committed; receipt/query times are not renewed.";
        case BillingOwnerAction::Reconcile:return "Record a proven settled or unchanged outcome for the Issued stage. This does not apply money or adopt ambiguous creation.";
        default:return "Unavailable action.";}}
    static const char *state(BillingOwnerState s){switch(s){case BillingOwnerState::Stopped:return "Stopped";case BillingOwnerState::Busy:return "Busy";case BillingOwnerState::Ready:return "Ready";case BillingOwnerState::NeedsReview:return "NeedsReview";case BillingOwnerState::ClockFault:return "ClockFault";case BillingOwnerState::Faulted:return "Faulted";}return "Unknown";}
    static const char *computeState(BillingComputeState s){switch(s){case BillingComputeState::Stopped:return "Stopped";case BillingComputeState::Waiting:return "Waiting";case BillingComputeState::Busy:return "Busy";case BillingComputeState::Ready:return "Ready";case BillingComputeState::Unavailable:return "Unavailable";case BillingComputeState::ClockFault:return "ClockFault";case BillingComputeState::Faulted:return "Faulted";}return "Unknown";}
    static const char *status(BillingStatus s){switch(s){case BillingStatus::Ok:return "Ok";case BillingStatus::Replay:return "Replay";case BillingStatus::Invalid:return "Invalid";case BillingStatus::Unsupported:return "Unsupported";case BillingStatus::Attribution:return "Attribution";case BillingStatus::Conflict:return "Conflict";case BillingStatus::SequenceGap:return "SequenceGap";case BillingStatus::Clock:return "Clock";case BillingStatus::Capacity:return "Capacity";case BillingStatus::Calculation:return "Calculation";case BillingStatus::Storage:return "UNAVAILABLE (Storage), not zero";}return "Unknown";}
    static const char *admission(BillingOwnerAdmission s){switch(s){case BillingOwnerAdmission::Queued:return "QUEUED once; not a durable acknowledgement.";case BillingOwnerAdmission::Stopped:return "Owner stopped; nothing queued.";case BillingOwnerAdmission::NotReady:return "Owner not ready; nothing queued.";case BillingOwnerAdmission::StaleGeneration:return "Generation changed; review again.";case BillingOwnerAdmission::ReviewChanged:return "Revision changed; review again.";case BillingOwnerAdmission::Invalid:return "Invalid request; original preserved.";}return "Nothing queued.";}
};
} // namespace om
