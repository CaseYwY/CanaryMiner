// Native bounded requests only. Opening/editing this page never issues a capture plan or touches storage directly.
#pragma once
#include "energy_authorization_owner.h"
#include "dashboard.h"
namespace om {
struct EnergyAuthorizationAccess {
    std::function<std::shared_ptr<const EnergyAuthorizationSnapshot>()> snapshot;
    std::function<EnergyHistoryAdmission(uint64_t,uint64_t,const EnergyAuthorizationCommand &)> submit;
    std::function<EnergyHistoryAdmission(uint64_t,uint64_t)> acknowledge;
    std::vector<std::string> selectedDevices; // caller's explicit selection, never discovered by the settings renderer
    bool captureActive=false;
    std::function<std::shared_ptr<const EnergyCaptureSnapshot>()> captureSnapshot;
};
enum class EnergyAuthorizationPanelResult { None,Close,Quit };
enum class EnergyAuthorizationPage { Browse,Edit,Review,Leave };
class EnergyAuthorizationPanel {
   public:
    bool visible=false,reveal=false,acknowledged=false,replaceInput=true,ackReview=false,manualScroll=false;
    unsigned field=0;
    size_t offset=0;
    uint64_t submittedTicket=0,submittedGeneration=0;
    EnergyAuthorizationPage page=EnergyAuthorizationPage::Browse;
    EnergyCapturePolicy draftPolicy;
    EnergyHistoryLimits draftLimits;
    EnergyAuthorizationCommand reviewedCommand;
    std::shared_ptr<const EnergyAuthorizationSnapshot> current,baseline,reviewed;
    std::string message,input;
    void open(std::shared_ptr<const EnergyAuthorizationSnapshot> s={}){
        visible=true;reveal=acknowledged=ackReview=manualScroll=false;field=0;offset=0;submittedTicket=submittedGeneration=0;
        page=EnergyAuthorizationPage::Browse;message.clear();current=std::move(s);resetDraft();
    }
    bool dirty()const{return baseline&&baseline->reply&&baseline->reply->document&&values(draftPolicy,draftLimits)!=values(baseline->reply->document->policy,baseline->reply->document->limits);}
    void resetDraft(){baseline=current;reviewed.reset();if(current&&current->reply&&current->reply->document){draftPolicy=current->reply->document->policy;draftLimits=current->reply->document->limits;}else{draftPolicy={};draftLimits={};}}
    void update(std::shared_ptr<const EnergyAuthorizationSnapshot> s){
        if(page==EnergyAuthorizationPage::Review&&(!s||!reviewed||s->generation!=reviewed->generation||s->revision!=reviewed->revision)){
            page=EnergyAuthorizationPage::Browse;reviewed.reset();acknowledged=ackReview=false;offset=0;message="Owner changed; review cancelled. Review current state again.";}
        const bool wasDirty=dirty();current=std::move(s);
        if(submittedTicket&&current&&current->generation==submittedGeneration&&!current->pending&&current->lastTicket==submittedTicket){
            if(current->uncertain)message="UNKNOWN outcome. F loads current state; A reviews the retained request. No automatic retry.";
            else if(current->reply&&current->reply->status==EnergyAuthorizationStatus::Ok){message="CONFIRMED saved. Next explicit parent only; current capture plan stays frozen.";resetDraft();}
            else message="Request not confirmed: "+std::string(current&&current->reply?status(current->reply->status):"helper unavailable")+". Preserve local stores.";
            submittedTicket=submittedGeneration=0;
        }else if(!wasDirty&&page!=EnergyAuthorizationPage::Edit&&page!=EnergyAuthorizationPage::Review)resetDraft();
    }
    EnergyAuthorizationPanelResult key(int key,const EnergyAuthorizationAccess &access={}){
        using P=EnergyAuthorizationPage;using A=EnergyAuthorizationAction;
        if(key==3||key=='q'||key=='Q')return EnergyAuthorizationPanelResult::Quit;
        if(page==P::Leave){if(key=='d'||key=='D'){resetDraft();visible=reveal=false;return EnergyAuthorizationPanelResult::Close;}if(key==27)page=P::Browse;return EnergyAuthorizationPanelResult::None;}
        if(page==P::Edit){
            if(key==27){page=P::Browse;message="Edit cancelled.";}
            else if(key=='\r'||key=='\n'){uint64_t n=0;if(input.empty()||input.size()>9)message="Enter a bounded whole number; blank is not zero.";
                else{for(char c:input)n=n*10+static_cast<unsigned>(c-'0');setNumber(n);page=P::Browse;message="Draft only. V reviews; Esc can discard.";}}
            else if(key==8||key==127){if(replaceInput)input.clear();else if(!input.empty())input.pop_back();replaceInput=false;}
            else if(key>='0'&&key<='9'){if(replaceInput)input.clear();replaceInput=false;if(input.size()<9)input+=static_cast<char>(key);}
            return EnergyAuthorizationPanelResult::None;
        }
        if(key=='i'||key=='I'){reveal=!reveal;acknowledged=false;offset=0;return EnergyAuthorizationPanelResult::None;}
        if(page==P::Review){
            if(key==27){page=P::Browse;reviewed.reset();acknowledged=ackReview=false;offset=0;message="Review cancelled; nothing queued.";}
            else if(key==' ')acknowledged=reveal?!acknowledged:false;
            else if(key=='\r'||key=='\n'||key==kKeyF2){
                const bool privateReview=ackReview||reviewedCommand.action!=A::Save;
                if(privateReview&&(!reveal||!acknowledged))message="I reveals the selected/private original; Space acknowledges after review.";
                else if(!reviewed)message="Review unavailable.";
                else{
                    const auto result=ackReview?(access.acknowledge?access.acknowledge(reviewed->generation,reviewed->revision):EnergyHistoryAdmission::NotReady):
                        (access.submit?access.submit(reviewed->generation,reviewed->revision,reviewedCommand):EnergyHistoryAdmission::NotReady);
                    if(result==EnergyHistoryAdmission::Queued&&!ackReview){submittedTicket=reviewed->lastTicket+1;submittedGeneration=reviewed->generation;}
                    message=result==EnergyHistoryAdmission::Queued?(ackReview?"Inspection acknowledged. Nothing retried; no capture plan issued.":"QUEUED; waiting for the helper's persisted acknowledgement."):"Review changed or request invalid; nothing queued.";
                    page=P::Browse;reviewed.reset();acknowledged=ackReview=false;offset=0;
                }
            }else scroll(key);
            return EnergyAuthorizationPanelResult::None;
        }
        if(key==27){if(dirty())page=P::Leave;else{visible=reveal=false;return EnergyAuthorizationPanelResult::Close;}}
        else if(key=='d'||key=='D'){resetDraft();message="Draft discarded; saved state unchanged.";}
        else if(key=='f'||key=='F'){
            if(dirty())message="Discard the local draft with D before inspecting current saved state.";
            else if(current&&access.submit){EnergyAuthorizationCommand c;c.action=!current->uncertain&&current->reply&&current->reply->document&&current->reply->document->enrollment==EnergyEnrollment::Pending?A::Inspect:A::Load;
                const auto result=access.submit(current->generation,current->revision,c);message=result==EnergyHistoryAdmission::Queued?"Inspection QUEUED; no save or creation requested.":"Inspection unavailable.";}
        }else if(key=='v'||key=='V'||key==kKeyF2)review(A::Save,access);
        else if(key=='e'||key=='E')review(A::Enroll,access);
        else if(key=='c'||key=='C')review(A::Confirm,access);
        else if(key=='a'||key=='A'){
            if(current&&current->uncertain&&current->inspectionAvailable&&access.acknowledge){reviewed=current;ackReview=true;page=P::Review;offset=0;acknowledged=false;}
            else message="No inspected unknown request to acknowledge. F loads saved state first.";
        }else if(key==kKeyUp||key==kKeyDown){field=(field+(key==kKeyDown?1:13))%14;offset=field;manualScroll=false;}
        else if(key==kKeyPageUp||key==kKeyPageDown){manualScroll=true;scroll(key);}
        else if(key=='\r'||key=='\n'||key==' '){
            if(!editable())message="No editable saved snapshot. Wait for the helper or reopen after resolving its error.";
            else if(field>=6&&baseline->reply->document->enrollment!=EnergyEnrollment::None)message="Retention limits are fixed by this enrollment. Existing history is not reinterpreted.";
            else if(field==0)draftPolicy.persistenceAuthorized=!draftPolicy.persistenceAuthorized;
            else if(field==11){if(draftLimits.compaction)draftLimits.compaction.reset();else draftLimits.compaction=EnergyCompactionPolicy{};}
            else if(field>=12&&!draftLimits.compaction)message="Enable exact compaction in this new-store draft before editing its budgets.";
            else{page=P::Edit;input=std::to_string(numbers()[field]);replaceInput=true;}
        }
        return EnergyAuthorizationPanelResult::None;
    }
    std::vector<std::string> lines(const EnergyAuthorizationAccess &access={})const{
        using P=EnergyAuthorizationPage;std::vector<std::string> out={"Energy persistence / independent authorization"};
        if(!current){out.push_back("UNAVAILABLE: no authorization helper owner supplied.");out.push_back("Session energy collection alone never permits disk retention.");return out;}
        std::string capture=access.captureActive?"frozen active plan":"not active";
        if(access.captureSnapshot){const auto s=access.captureSnapshot();if(s){
            if(s->state==EnergyCaptureState::Collecting)capture="frozen active plan";
            else if(s->state==EnergyCaptureState::Starting)capture="starting / awaiting helper";
            else if(s->state==EnergyCaptureState::NeedsReview)capture="paused / recovery review required";
            else if(s->state==EnergyCaptureState::Faulted||s->state==EnergyCaptureState::ClockFault)capture="unavailable / preserve local history";
            else capture="not active";}}
        out.push_back("Current capture: "+capture+(current->cliNoEnergy?" | --no-energy overrides provisioning":""));
        if(current->startupPending)out.push_back("STARTING: freezing this parent's capture decision. Settings edits wait for a fresh snapshot.");
        if(access.selectedDevices.empty())out.push_back("Enrollment unavailable: select full explicit GPU UUIDs; idle settings does not discover GPUs.");
        out.push_back(current->pending?"BUSY: bounded helper request; admission is not confirmation.":"IDLE: settings do not start or restart mining.");
        if(current->reply)out.push_back("Last result: "+std::string(status(current->reply->status)));
        else if(current->state==EnergyAuthorizationOwnerState::Faulted)out.push_back("UNAVAILABLE: authorization helper failed; preserve local stores.");
        if(current->uncertain)out.push_back("UNKNOWN retained original; new mutations blocked until reviewed inspection.");
        if(page==P::Leave){out.push_back("Unsaved draft. D discards and closes; Esc returns to editing.");return out;}
        if(page==P::Edit){out.push_back(std::string(labels()[field])+" = "+input+"_");out.push_back("Whole numbers only. Enter keeps draft; Esc cancels this edit.");return out;}
        const auto doc=current->reply&&current->reply->document?&*current->reply->document:nullptr;
        out.push_back("Enrollment: "+std::string(!doc?"unavailable":doc->enrollment==EnergyEnrollment::None?"none":doc->enrollment==EnergyEnrollment::Pending?"PENDING - never auto-create again":"ready / immutable identity and retention"));
        if(page==P::Review){
            out.push_back(ackReview?"REVIEW INSPECTION acknowledgement":reviewedCommand.action==EnergyAuthorizationAction::Enroll?"REVIEW ENROLLMENT / creates a new private store":reviewedCommand.action==EnergyAuthorizationAction::Confirm?"REVIEW CONFIRM / exact empty store only":"REVIEW SAVE / next explicit parent only");
            out.push_back("GPU board sample-and-hold; gaps remain unknown. Finite budgets reject at capacity.");
            out.push_back("Exact compaction expires raw originals, not aggregate energy; no guaranteed retention days.");
        }
        const bool reviewValues=page==P::Review&&!ackReview&&(reviewedCommand.action==EnergyAuthorizationAction::Save||reviewedCommand.action==EnergyAuthorizationAction::Enroll);
        const bool originalValues=ackReview&&current->uncertain&&(current->uncertain->action==EnergyAuthorizationAction::Save||current->uncertain->action==EnergyAuthorizationAction::Enroll);
        const auto p=originalValues?current->uncertain->policy:reviewValues?reviewedCommand.policy:draftPolicy;
        const auto l=originalValues?current->uncertain->limits:reviewValues?reviewedCommand.limits:draftLimits;
        const auto v=values(p,l);for(size_t i=0;i<v.size();++i)out.push_back(std::string(page==P::Browse&&i==field?"> ":"  ")+labels()[i]+": "+v[i]);
        if(!reveal)out.push_back("PRIVATE selected UUIDs, namespace, scope, session and original timestamps hidden. I reveals locally.");
        else{
            const auto &ids=ackReview&&current->uncertain?current->uncertain->devices:access.selectedDevices;
            for(size_t i=0;i<ids.size()&&i<64;++i)out.push_back("Selected UUID "+std::to_string(i+1)+": "+safe(ids[i]));
            if(doc&&doc->enrollment!=EnergyEnrollment::None){out.push_back("Namespace "+safe(doc->privateNamespace));out.push_back("Scope "+safe(doc->binding.evidence.scope));
                out.push_back("History begin UTC ms "+std::to_string(doc->binding.historyBeginUtcMs));out.push_back("Last reserved session "+safe(doc->lastSessionId));
                for(const auto &channel:doc->binding.channels)out.push_back("Channel "+safe(channel));}
            if(ackReview&&current->uncertain){const auto &r=*current->uncertain;out.push_back("ORIGINAL action "+std::to_string(static_cast<unsigned>(r.action))+" | UTC ms "+std::to_string(r.nowUtcMs));out.push_back("Original CAS "+safe(r.expected));}
        }
        if(page==P::Review&&(ackReview||reviewedCommand.action!=EnergyAuthorizationAction::Save))out.push_back(std::string(acknowledged?"[x]":"[ ]")+" Reviewed exact selection / private state; Space acknowledges.");
        out.push_back("Saved authorization affects only the next explicit parent. No collection or plan issuance on this page.");
        return out;
    }
    std::string render(int columns,int rows,ColorLevel color,const EnergyAuthorizationAccess &access={})const{
        using namespace dash_detail;const size_t width=static_cast<size_t>((std::max)(20,columns)-1);
        if(rows>0&&rows<12)return "Terminal too short.\nResize to 12+ rows.\n[Q/Ctrl+C] Stop\n";
        const auto footer=wrapText(page==EnergyAuthorizationPage::Review?"[I] Reveal [Space] Ack [Enter] Submit [Arrows] Scroll [Esc] Cancel [Q] Stop":page==EnergyAuthorizationPage::Edit?
            "[Enter] Keep draft [Esc] Cancel [Q/Ctrl+C] Stop":"[V] Save review [E] Enroll [C] Confirm [F] Inspect [A] Ack [I] Reveal [Arrows] Move [Esc] Back [Q] Stop",width);
        auto note=wrapText(message.empty()?"Independent opt-in; durable acknowledgement required.":message,width);if(note.size()>2)note.resize(2);
        const auto budget=rows>0?static_cast<size_t>((std::max)(1,rows-static_cast<int>(footer.size()+note.size())-1)):size_t(10000);
        std::vector<std::string> content;for(const auto &line:lines(access))for(const auto &part:wrapText(line,width))content.push_back(part);
        size_t requested=offset;
        if(page==EnergyAuthorizationPage::Browse&&!manualScroll){size_t selected=0;for(size_t i=0;i<content.size();++i)if(content[i].rfind("> ",0)==0){selected=i;break;}
            if(selected>=requested+budget)requested=selected-budget+1;else if(selected<requested)requested=selected;}
        const auto start=(std::min)(requested,content.size()>budget?content.size()-budget:size_t(0));std::string out;
        for(size_t i=start;i<content.size()&&i<start+budget;++i)out+=paint(content[i],content[i].rfind("> ",0)==0?Tone::Canary:Tone::Plain,color)+"\n";
        for(const auto &line:note)out+=line+"\n";
        for(const auto &line:footer)out+=line+"\n";
        return out;
    }
   private:
    bool editable()const{return current&&baseline&&current->reply&&baseline->reply&&baseline->reply->document&&current->reply->token==baseline->reply->token&&!current->pending&&!current->uncertain&&current->state==EnergyAuthorizationOwnerState::Ready;}
    void review(EnergyAuthorizationAction action,const EnergyAuthorizationAccess &access){
        if(!editable()||!access.submit){message="Saved baseline unavailable or changed; discard/reload before review.";return;}
        const auto &d=*baseline->reply->document;
        if(action==EnergyAuthorizationAction::Enroll){
            bool bounded=!access.selectedDevices.empty()&&access.selectedDevices.size()<=64;
            if(bounded)for(const auto &id:access.selectedDevices)if(id.size()!=40)bounded=false;
            if(d.enrollment!=EnergyEnrollment::None||!bounded){message="Enrollment requires a bounded explicit selected set and no existing reservation.";return;}
        }
        if(action==EnergyAuthorizationAction::Confirm&&(d.enrollment!=EnergyEnrollment::Pending||!current->reply->emptyStoreVerified)){message="F must inspect the exact Pending empty store before confirmation.";return;}
        reviewed=current;reviewedCommand={};reviewedCommand.action=action;
        if(action==EnergyAuthorizationAction::Save||action==EnergyAuthorizationAction::Enroll){reviewedCommand.policy=draftPolicy;reviewedCommand.limits=draftLimits;}
        if(action==EnergyAuthorizationAction::Enroll){reviewedCommand.policy.persistenceAuthorized=false;reviewedCommand.devices=access.selectedDevices;}
        page=EnergyAuthorizationPage::Review;offset=0;acknowledged=ackReview=false;message.clear();
    }
    void scroll(int key){if(key==kKeyUp||key==kKeyPageUp){const size_t step=key==kKeyUp?1:5;offset=offset>step?offset-step:0;}
        else if(key==kKeyDown||key==kKeyPageDown)offset=(std::min)(size_t(2048),offset+(key==kKeyDown?1:5));}
    static std::vector<const char *> labels(){return {"Persist board-energy history","Selection ms [500..60000]","Maximum gap ms [selection..60000]","Flush ms [50..60000]","Buffered readings [1..128]","Batch readings [1..buffer]",
        "Raw batches [1..4096]","Raw readings [1..131072]","Lifetime sessions [1..1024]","Lifetime windows [1..4096]","Database bytes [1..128 MiB]","Exact compaction (new store)","Compact after batches [1..raw]","Exact segments [1..32768]"};}
    static std::vector<std::string> values(const EnergyCapturePolicy &p,const EnergyHistoryLimits &l){return {p.persistenceAuthorized?"ON":"OFF",std::to_string(p.selectionMs),std::to_string(p.maximumGapMs),std::to_string(p.flushMs),std::to_string(p.bufferedReadings),std::to_string(p.batchReadings),
        std::to_string(l.maxBatches),std::to_string(l.maxReadings),std::to_string(l.maxSessions),std::to_string(l.maxWindows),std::to_string(l.maxDatabaseBytes),l.compaction?"ON":"OFF",l.compaction?std::to_string(l.compaction->afterBatches):"unavailable",l.compaction?std::to_string(l.compaction->maxSegments):"unavailable"};}
    std::vector<uint64_t> numbers()const{return {0,draftPolicy.selectionMs,draftPolicy.maximumGapMs,draftPolicy.flushMs,draftPolicy.bufferedReadings,draftPolicy.batchReadings,draftLimits.maxBatches,draftLimits.maxReadings,draftLimits.maxSessions,draftLimits.maxWindows,draftLimits.maxDatabaseBytes,0,draftLimits.compaction?draftLimits.compaction->afterBatches:0,draftLimits.compaction?draftLimits.compaction->maxSegments:0};}
    void setNumber(uint64_t n){switch(field){case 1:draftPolicy.selectionMs=static_cast<unsigned>(n);break;case 2:draftPolicy.maximumGapMs=static_cast<unsigned>(n);break;case 3:draftPolicy.flushMs=static_cast<unsigned>(n);break;
        case 4:draftPolicy.bufferedReadings=static_cast<size_t>(n);break;case 5:draftPolicy.batchReadings=static_cast<size_t>(n);break;case 6:draftLimits.maxBatches=static_cast<size_t>(n);break;case 7:draftLimits.maxReadings=static_cast<size_t>(n);break;
        case 8:draftLimits.maxSessions=static_cast<size_t>(n);break;case 9:draftLimits.maxWindows=static_cast<size_t>(n);break;case 10:draftLimits.maxDatabaseBytes=n;break;
        case 12:if(draftLimits.compaction)draftLimits.compaction->afterBatches=static_cast<size_t>(n);break;case 13:if(draftLimits.compaction)draftLimits.compaction->maxSegments=static_cast<size_t>(n);break;default:break;}}
    static std::string safe(const std::string &s){std::string out;for(unsigned char c:s){if(out.size()==128)break;out+=c>=32&&c<=126?static_cast<char>(c):'?';}return out;}
    static const char *status(EnergyAuthorizationStatus s){switch(s){case EnergyAuthorizationStatus::Ok:return "Ok";case EnergyAuthorizationStatus::Missing:return "Missing / disabled defaults";case EnergyAuthorizationStatus::Invalid:return "Invalid";case EnergyAuthorizationStatus::Conflict:return "Conflict";case EnergyAuthorizationStatus::Storage:return "Storage unavailable";case EnergyAuthorizationStatus::Clock:return "Clock fault";case EnergyAuthorizationStatus::Selection:return "Selection mismatch";case EnergyAuthorizationStatus::Disabled:return "Disabled";case EnergyAuthorizationStatus::NeedsReview:return "NeedsReview";case EnergyAuthorizationStatus::Entropy:return "System entropy unavailable";}return "Unknown";}
};
} // namespace om
