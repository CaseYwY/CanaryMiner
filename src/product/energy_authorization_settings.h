// Native bounded requests only. Opening/editing this page never issues a capture plan or touches storage directly.
#pragma once
#include "energy_authorization_owner.h"
#include "dashboard.h"
#include "energy_authorization_field_messages.h"
#include "energy_authorization_notice_messages.h"
#include "energy_authorization_panel_messages.h"
#include "energy_authorization_value_messages.h"
#include "energy_authorization_private_messages.h"
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
        using NoticeKey=i18n::EnergyAuthorizationNoticeMessageKey;
        if(page==EnergyAuthorizationPage::Review&&(!s||!reviewed||s->generation!=reviewed->generation||s->revision!=reviewed->revision)){
            page=EnergyAuthorizationPage::Browse;reviewed.reset();acknowledged=ackReview=false;offset=0;message=i18n::energyAuthorizationNoticeEnglish(NoticeKey::OwnerChanged);}
        const bool wasDirty=dirty();current=std::move(s);
        if(submittedTicket&&current&&current->generation==submittedGeneration&&!current->pending&&current->lastTicket==submittedTicket){
            if(current->uncertain)message=i18n::energyAuthorizationNoticeEnglish(NoticeKey::UnknownOutcome);
            else if(current->reply&&current->reply->status==EnergyAuthorizationStatus::Ok){message=i18n::energyAuthorizationNoticeEnglish(NoticeKey::ConfirmedSaved);resetDraft();}
            else message="Request not confirmed: "+std::string(current&&current->reply?status(current->reply->status):"helper unavailable")+". Preserve local stores.";
            submittedTicket=submittedGeneration=0;
        }else if(!wasDirty&&page!=EnergyAuthorizationPage::Edit&&page!=EnergyAuthorizationPage::Review)resetDraft();
    }
    EnergyAuthorizationPanelResult key(int key,const EnergyAuthorizationAccess &access={}){
        using P=EnergyAuthorizationPage;using A=EnergyAuthorizationAction;
        using NoticeKey=i18n::EnergyAuthorizationNoticeMessageKey;
        if(key==3||key=='q'||key=='Q')return EnergyAuthorizationPanelResult::Quit;
        if(page==P::Leave){if(key=='d'||key=='D'){resetDraft();visible=reveal=false;return EnergyAuthorizationPanelResult::Close;}if(key==27)page=P::Browse;return EnergyAuthorizationPanelResult::None;}
        if(page==P::Edit){
            if(key==27){page=P::Browse;message=i18n::energyAuthorizationNoticeEnglish(NoticeKey::EditCancelled);}
            else if(key=='\r'||key=='\n'){uint64_t n=0;if(input.empty()||input.size()>9)message=i18n::energyAuthorizationNoticeEnglish(NoticeKey::InvalidInteger);
                else{for(char c:input)n=n*10+static_cast<unsigned>(c-'0');setNumber(n);page=P::Browse;message=i18n::energyAuthorizationNoticeEnglish(NoticeKey::DraftOnly);}}
            else if(key==8||key==127){if(replaceInput)input.clear();else if(!input.empty())input.pop_back();replaceInput=false;}
            else if(key>='0'&&key<='9'){if(replaceInput)input.clear();replaceInput=false;if(input.size()<9)input+=static_cast<char>(key);}
            return EnergyAuthorizationPanelResult::None;
        }
        if(key=='i'||key=='I'){reveal=!reveal;acknowledged=false;offset=0;return EnergyAuthorizationPanelResult::None;}
        if(page==P::Review){
            if(key==27){page=P::Browse;reviewed.reset();acknowledged=ackReview=false;offset=0;message=i18n::energyAuthorizationNoticeEnglish(NoticeKey::ReviewCancelled);}
            else if(key==' ')acknowledged=reveal?!acknowledged:false;
            else if(key=='\r'||key=='\n'||key==kKeyF2){
                const bool privateReview=ackReview||reviewedCommand.action!=A::Save;
                if(privateReview&&(!reveal||!acknowledged))message=i18n::energyAuthorizationNoticeEnglish(NoticeKey::NeedRevealAck);
                else if(!reviewed)message=i18n::energyAuthorizationNoticeEnglish(NoticeKey::ReviewUnavailable);
                else{
                    const auto result=ackReview?(access.acknowledge?access.acknowledge(reviewed->generation,reviewed->revision):EnergyHistoryAdmission::NotReady):
                        (access.submit?access.submit(reviewed->generation,reviewed->revision,reviewedCommand):EnergyHistoryAdmission::NotReady);
                    if(result==EnergyHistoryAdmission::Queued&&!ackReview){submittedTicket=reviewed->lastTicket+1;submittedGeneration=reviewed->generation;}
                    message=i18n::energyAuthorizationNoticeEnglish(result==EnergyHistoryAdmission::Queued?(ackReview?NoticeKey::InspectionAck:NoticeKey::Queued):NoticeKey::ReviewChanged);
                    page=P::Browse;reviewed.reset();acknowledged=ackReview=false;offset=0;
                }
            }else scroll(key);
            return EnergyAuthorizationPanelResult::None;
        }
        if(key==27){if(dirty())page=P::Leave;else{visible=reveal=false;return EnergyAuthorizationPanelResult::Close;}}
        else if(key=='d'||key=='D'){resetDraft();message=i18n::energyAuthorizationNoticeEnglish(NoticeKey::DraftDiscarded);}
        else if(key=='f'||key=='F'){
            if(dirty())message=i18n::energyAuthorizationNoticeEnglish(NoticeKey::DiscardBeforeInspect);
            else if(current&&access.submit){EnergyAuthorizationCommand c;c.action=!current->uncertain&&current->reply&&current->reply->document&&current->reply->document->enrollment==EnergyEnrollment::Pending?A::Inspect:A::Load;
                const auto result=access.submit(current->generation,current->revision,c);message=i18n::energyAuthorizationNoticeEnglish(result==EnergyHistoryAdmission::Queued?NoticeKey::InspectionQueued:NoticeKey::InspectionUnavailable);}
        }else if(key=='v'||key=='V'||key==kKeyF2)review(A::Save,access);
        else if(key=='e'||key=='E')review(A::Enroll,access);
        else if(key=='c'||key=='C')review(A::Confirm,access);
        else if(key=='a'||key=='A'){
            if(current&&current->uncertain&&current->inspectionAvailable&&access.acknowledge){reviewed=current;ackReview=true;page=P::Review;offset=0;acknowledged=false;}
            else message=i18n::energyAuthorizationNoticeEnglish(NoticeKey::NoUnknown);
        }else if(key==kKeyUp||key==kKeyDown){field=(field+(key==kKeyDown?1:13))%14;offset=field;manualScroll=false;}
        else if(key==kKeyPageUp||key==kKeyPageDown){manualScroll=true;scroll(key);}
        else if(key=='\r'||key=='\n'||key==' '){
            if(!editable())message=i18n::energyAuthorizationNoticeEnglish(NoticeKey::NoEditable);
            else if(field>=6&&baseline->reply->document->enrollment!=EnergyEnrollment::None)message=i18n::energyAuthorizationNoticeEnglish(NoticeKey::RetentionFixed);
            else if(field==0)draftPolicy.persistenceAuthorized=!draftPolicy.persistenceAuthorized;
            else if(field==11){if(draftLimits.compaction)draftLimits.compaction.reset();else draftLimits.compaction=EnergyCompactionPolicy{};}
            else if(field>=12&&!draftLimits.compaction)message=i18n::energyAuthorizationNoticeEnglish(NoticeKey::NeedCompaction);
            else{page=P::Edit;input=std::to_string(numbers()[field]);replaceInput=true;}
        }
        return EnergyAuthorizationPanelResult::None;
    }
    std::vector<std::string> lines(const EnergyAuthorizationAccess &access={})const{
        using P=EnergyAuthorizationPage;using PanelKey=i18n::EnergyAuthorizationPanelMessageKey;std::vector<std::string> out={i18n::energyAuthorizationPanelEnglish(PanelKey::Title)};
        using ValueKey=i18n::EnergyAuthorizationValueMessageKey;
        if(!current){out.push_back(i18n::energyAuthorizationPanelEnglish(PanelKey::Unavailable));out.push_back(i18n::energyAuthorizationPanelEnglish(PanelKey::NoRetention));return out;}
        std::string capture=i18n::energyAuthorizationValueEnglish(access.captureActive?ValueKey::CaptureFrozen:ValueKey::CaptureInactive);
        if(access.captureSnapshot){const auto s=access.captureSnapshot();if(s){
            if(s->state==EnergyCaptureState::Collecting)capture=i18n::energyAuthorizationValueEnglish(ValueKey::CaptureFrozen);
            else if(s->state==EnergyCaptureState::Starting)capture=i18n::energyAuthorizationValueEnglish(ValueKey::CaptureStarting);
            else if(s->state==EnergyCaptureState::NeedsReview)capture=i18n::energyAuthorizationValueEnglish(ValueKey::CaptureReview);
            else if(s->state==EnergyCaptureState::Faulted||s->state==EnergyCaptureState::ClockFault)capture=i18n::energyAuthorizationValueEnglish(ValueKey::CaptureUnavailable);
            else capture=i18n::energyAuthorizationValueEnglish(ValueKey::CaptureInactive);}}
        out.push_back(i18n::energyAuthorizationValueEnglish(current->cliNoEnergy?ValueKey::CaptureCli:ValueKey::CurrentCapture,{{"state",capture}}));
        if(current->startupPending)out.push_back(i18n::energyAuthorizationPanelEnglish(PanelKey::StartupPending));
        if(access.selectedDevices.empty())out.push_back(i18n::energyAuthorizationPanelEnglish(PanelKey::NeedSelection));
        out.push_back(i18n::energyAuthorizationPanelEnglish(current->pending?PanelKey::Busy:PanelKey::Idle));
        if(current->reply)out.push_back(i18n::energyAuthorizationValueEnglish(ValueKey::LastResult,{{"state",status(current->reply->status)}}));
        else if(current->state==EnergyAuthorizationOwnerState::Faulted)out.push_back(i18n::energyAuthorizationPanelEnglish(PanelKey::HelperFailed));
        if(current->uncertain)out.push_back(i18n::energyAuthorizationPanelEnglish(PanelKey::UnknownRetained));
        if(page==P::Leave){out.push_back(i18n::energyAuthorizationPanelEnglish(PanelKey::Leave));return out;}
        if(page==P::Edit){out.push_back(i18n::energyAuthorizationValueEnglish(ValueKey::EditInput,{{"field",labels()[field]},{"input",input}}));out.push_back(i18n::energyAuthorizationPanelEnglish(PanelKey::EditGuide));return out;}
        const auto doc=current->reply&&current->reply->document?&*current->reply->document:nullptr;
        out.push_back(i18n::energyAuthorizationValueEnglish(ValueKey::Enrollment,{{"state",i18n::energyAuthorizationValueEnglish(!doc?ValueKey::Unavailable:doc->enrollment==EnergyEnrollment::None?ValueKey::EnrollmentNone:doc->enrollment==EnergyEnrollment::Pending?ValueKey::EnrollmentPending:ValueKey::EnrollmentReady)}}));
        if(page==P::Review){
            out.push_back(i18n::energyAuthorizationPanelEnglish(ackReview?PanelKey::ReviewInspection:reviewedCommand.action==EnergyAuthorizationAction::Enroll?PanelKey::ReviewEnroll:reviewedCommand.action==EnergyAuthorizationAction::Confirm?PanelKey::ReviewConfirm:PanelKey::ReviewSave));
            out.push_back(i18n::energyAuthorizationPanelEnglish(PanelKey::SampleHold));
            out.push_back(i18n::energyAuthorizationPanelEnglish(PanelKey::CompactionExpires));
        }
        const bool reviewValues=page==P::Review&&!ackReview&&(reviewedCommand.action==EnergyAuthorizationAction::Save||reviewedCommand.action==EnergyAuthorizationAction::Enroll);
        const bool originalValues=ackReview&&current->uncertain&&(current->uncertain->action==EnergyAuthorizationAction::Save||current->uncertain->action==EnergyAuthorizationAction::Enroll);
        const auto p=originalValues?current->uncertain->policy:reviewValues?reviewedCommand.policy:draftPolicy;
        const auto l=originalValues?current->uncertain->limits:reviewValues?reviewedCommand.limits:draftLimits;
        const auto v=values(p,l);for(size_t i=0;i<v.size();++i){const bool human=i==0||i==11||(i>=12&&!l.compaction);
            out.push_back(std::string(page==P::Browse&&i==field?"> ":"  ")+i18n::energyAuthorizationValueEnglish(human?ValueKey::FieldHuman:ValueKey::FieldRequest,{{"field",labels()[i]},{"value",v[i]}}));}
        if(!reveal)out.push_back(i18n::energyAuthorizationPanelEnglish(PanelKey::PrivateHidden));
        else{
            using PrivateKey=i18n::EnergyAuthorizationPrivateMessageKey;
            const auto &ids=ackReview&&current->uncertain?current->uncertain->devices:access.selectedDevices;
            for(size_t i=0;i<ids.size()&&i<64;++i)out.push_back(privateIdentityLine(PrivateKey::SelectedUuid,PrivateKey::SelectedUuidEmpty,ids[i],{{"position",std::to_string(i+1)}}));
            if(doc&&doc->enrollment!=EnergyEnrollment::None){out.push_back(privateIdentityLine(PrivateKey::Namespace,PrivateKey::NamespaceEmpty,doc->privateNamespace));out.push_back(privateIdentityLine(PrivateKey::Scope,PrivateKey::ScopeEmpty,doc->binding.evidence.scope));
                out.push_back(i18n::energyAuthorizationPrivateEnglish(PrivateKey::HistoryBegin,{{"instant",std::to_string(doc->binding.historyBeginUtcMs)}}));out.push_back(privateIdentityLine(PrivateKey::LastSession,PrivateKey::LastSessionEmpty,doc->lastSessionId));
                for(const auto &channel:doc->binding.channels)out.push_back(privateIdentityLine(PrivateKey::Channel,PrivateKey::ChannelEmpty,channel));}
            if(ackReview&&current->uncertain){const auto &r=*current->uncertain;out.push_back(i18n::energyAuthorizationPrivateEnglish(PrivateKey::OriginalAction,{{"action",std::to_string(static_cast<unsigned>(r.action))},{"instant",std::to_string(r.nowUtcMs)}}));out.push_back(privateIdentityLine(PrivateKey::OriginalCas,PrivateKey::OriginalCasEmpty,r.expected));}
        }
        if(page==P::Review&&(ackReview||reviewedCommand.action!=EnergyAuthorizationAction::Save))out.push_back(i18n::energyAuthorizationValueEnglish(acknowledged?ValueKey::AckChecked:ValueKey::AckUnchecked));
        out.push_back(i18n::energyAuthorizationPanelEnglish(PanelKey::NextParent));
        return out;
    }
    std::string render(int columns,int rows,ColorLevel color,const EnergyAuthorizationAccess &access={})const{
        using namespace dash_detail;const size_t width=static_cast<size_t>((std::max)(20,columns)-1);
        using PanelKey=i18n::EnergyAuthorizationPanelMessageKey;
        if(rows>0&&rows<12)return i18n::energyAuthorizationPanelEnglish(PanelKey::ShortTitle)+"\n"+i18n::energyAuthorizationPanelEnglish(PanelKey::ShortResize)+"\n"+i18n::energyAuthorizationPanelEnglish(PanelKey::ShortKeys)+"\n";
        const auto footer=wrapText(i18n::energyAuthorizationPanelEnglish(page==EnergyAuthorizationPage::Review?PanelKey::FooterReview:page==EnergyAuthorizationPage::Edit?PanelKey::FooterEdit:PanelKey::FooterBrowse),width);
        auto note=wrapText(message.empty()?i18n::energyAuthorizationPanelEnglish(PanelKey::LocalDefault):message,width);if(note.size()>2)note.resize(2);
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
        using NoticeKey=i18n::EnergyAuthorizationNoticeMessageKey;
        if(!editable()||!access.submit){message=i18n::energyAuthorizationNoticeEnglish(NoticeKey::BaselineUnavailable);return;}
        const auto &d=*baseline->reply->document;
        if(action==EnergyAuthorizationAction::Enroll){
            bool bounded=!access.selectedDevices.empty()&&access.selectedDevices.size()<=64;
            if(bounded)for(const auto &id:access.selectedDevices)if(id.size()!=40)bounded=false;
            if(d.enrollment!=EnergyEnrollment::None||!bounded){message=i18n::energyAuthorizationNoticeEnglish(NoticeKey::EnrollmentRequired);return;}
        }
        if(action==EnergyAuthorizationAction::Confirm&&(d.enrollment!=EnergyEnrollment::Pending||!current->reply->emptyStoreVerified)){message=i18n::energyAuthorizationNoticeEnglish(NoticeKey::PendingInspectRequired);return;}
        reviewed=current;reviewedCommand={};reviewedCommand.action=action;
        if(action==EnergyAuthorizationAction::Save||action==EnergyAuthorizationAction::Enroll){reviewedCommand.policy=draftPolicy;reviewedCommand.limits=draftLimits;}
        if(action==EnergyAuthorizationAction::Enroll){reviewedCommand.policy.persistenceAuthorized=false;reviewedCommand.devices=access.selectedDevices;}
        page=EnergyAuthorizationPage::Review;offset=0;acknowledged=ackReview=false;message.clear();
    }
    void scroll(int key){if(key==kKeyUp||key==kKeyPageUp){const size_t step=key==kKeyUp?1:5;offset=offset>step?offset-step:0;}
        else if(key==kKeyDown||key==kKeyPageDown)offset=(std::min)(size_t(2048),offset+(key==kKeyDown?1:5));}
    static const std::array<std::string,14> &labels(){
        static const std::array<std::string,14> names=[](){std::array<std::string,14> result;const auto &keys=i18n::energyAuthorizationFieldKeys();
            for(std::size_t i=0;i<result.size();++i){result[i]=i18n::energyAuthorizationFieldEnglish(keys[i]);}return result;}();return names;
    }
    static std::vector<std::string> values(const EnergyCapturePolicy &p,const EnergyHistoryLimits &l){using Key=i18n::EnergyAuthorizationValueMessageKey;return {i18n::energyAuthorizationValueEnglish(p.persistenceAuthorized?Key::On:Key::Off),std::to_string(p.selectionMs),std::to_string(p.maximumGapMs),std::to_string(p.flushMs),std::to_string(p.bufferedReadings),std::to_string(p.batchReadings),
        std::to_string(l.maxBatches),std::to_string(l.maxReadings),std::to_string(l.maxSessions),std::to_string(l.maxWindows),std::to_string(l.maxDatabaseBytes),i18n::energyAuthorizationValueEnglish(l.compaction?Key::On:Key::Off),l.compaction?std::to_string(l.compaction->afterBatches):i18n::energyAuthorizationValueEnglish(Key::Unavailable),l.compaction?std::to_string(l.compaction->maxSegments):i18n::energyAuthorizationValueEnglish(Key::Unavailable)};}
    std::vector<uint64_t> numbers()const{return {0,draftPolicy.selectionMs,draftPolicy.maximumGapMs,draftPolicy.flushMs,draftPolicy.bufferedReadings,draftPolicy.batchReadings,draftLimits.maxBatches,draftLimits.maxReadings,draftLimits.maxSessions,draftLimits.maxWindows,draftLimits.maxDatabaseBytes,0,draftLimits.compaction?draftLimits.compaction->afterBatches:0,draftLimits.compaction?draftLimits.compaction->maxSegments:0};}
    void setNumber(uint64_t n){switch(field){case 1:draftPolicy.selectionMs=static_cast<unsigned>(n);break;case 2:draftPolicy.maximumGapMs=static_cast<unsigned>(n);break;case 3:draftPolicy.flushMs=static_cast<unsigned>(n);break;
        case 4:draftPolicy.bufferedReadings=static_cast<size_t>(n);break;case 5:draftPolicy.batchReadings=static_cast<size_t>(n);break;case 6:draftLimits.maxBatches=static_cast<size_t>(n);break;case 7:draftLimits.maxReadings=static_cast<size_t>(n);break;
        case 8:draftLimits.maxSessions=static_cast<size_t>(n);break;case 9:draftLimits.maxWindows=static_cast<size_t>(n);break;case 10:draftLimits.maxDatabaseBytes=n;break;
        case 12:if(draftLimits.compaction)draftLimits.compaction->afterBatches=static_cast<size_t>(n);break;case 13:if(draftLimits.compaction)draftLimits.compaction->maxSegments=static_cast<size_t>(n);break;default:break;}}
    static std::string safe(const std::string &s){std::string out;for(unsigned char c:s){if(out.size()==128)break;out+=c>=32&&c<=126?static_cast<char>(c):'?';}return out;}
    static std::string privateIdentityLine(i18n::EnergyAuthorizationPrivateMessageKey key,i18n::EnergyAuthorizationPrivateMessageKey empty,const std::string &identity,i18n::MessageArguments arguments={}){
        const auto display=safe(identity);if(!display.empty())arguments.emplace_back("identity",display);
        return i18n::energyAuthorizationPrivateEnglish(display.empty()?empty:key,arguments);
    }
    static std::string status(EnergyAuthorizationStatus s){using Key=i18n::EnergyAuthorizationValueMessageKey;auto key=Key::StatusUnknown;
        switch(s){case EnergyAuthorizationStatus::Ok:key=Key::StatusOk;break;case EnergyAuthorizationStatus::Missing:key=Key::StatusMissing;break;case EnergyAuthorizationStatus::Invalid:key=Key::StatusInvalid;break;case EnergyAuthorizationStatus::Conflict:key=Key::StatusConflict;break;case EnergyAuthorizationStatus::Storage:key=Key::StatusStorage;break;case EnergyAuthorizationStatus::Clock:key=Key::StatusClock;break;case EnergyAuthorizationStatus::Selection:key=Key::StatusSelection;break;case EnergyAuthorizationStatus::Disabled:key=Key::StatusDisabled;break;case EnergyAuthorizationStatus::NeedsReview:key=Key::StatusNeedsReview;break;case EnergyAuthorizationStatus::Entropy:key=Key::StatusEntropy;break;}return i18n::energyAuthorizationValueEnglish(key);}
};
} // namespace om
