// Memory-only native panel bridge. Disk/helper I/O belongs to PreferencesOwner's background thread.
#pragma once
#include "preferences_owner.h"
#include "settings.h"
#include <string_view>
#include <type_traits>
namespace om {
struct PreferencesSettingsContext {
    RuntimeContext runtime;GameSettingsContext game;PoolSecuritySettingsContext pool;LanguageSettingsContext language;
    ManualMarketContext manual;ElectricitySettingsContext electricity;
    bool manualProjectionAvailable=false,publicReferencesRequested=false;
    ManualMarketFactory manualFactory; // same existing pure display factory; prepare at original submission, never acknowledgement
};
struct PreferencesSettingsCompletion {
    std::shared_ptr<const PreferencesReply> reply;
    uint64_t submittedUtc=0,submittedMono=0; // original submission, never acknowledgement/restamping time
    ManualMarketProjector manualProjection;
    ManualMarketPreferences manualValue;
};
static_assert(std::is_nothrow_move_assignable<SettingsPanel>::value,"Confirmed panel publication must not allocate after acknowledgement");
static_assert(std::is_nothrow_move_assignable<PreferencesSettingsContext>::value,"Admitted frozen context publication must not allocate");
class PreferencesSettings {
  public:
    PreferencesSettings(PreferencesOwner &owner,SettingsPanel &panel):owner_(owner),panel_(panel){}
    PreferencesSettingsContext context;
    bool load(PreferencesKind kind){
        if(owner_.foreignCaller())return false;
        view_=owner_.snapshot();
        if(view_.uncertain){panel_.visible=true;return false;}
        if(ticket_){notice("Preference operation pending; NOT saved. No duplicate request sent.");return false;}
        return admit(kind,PreferencesOperation::Load,false);
    }
    SettingsResult key(int key,uint64_t utc,uint64_t mono){
        if(owner_.foreignCaller())return SettingsResult::None;
        view_=owner_.snapshot();
        if(ticket_||view_.uncertain){
            if(key==3||key=='q'||key=='Q')return SettingsResult::Quit;
            if(ticket_){if(key==27){owner_.requestStop();ticket_=0;receipt_.reset();view_=owner_.snapshot();notice("Preference owner cancelled for this session; an issued write may be UNKNOWN. No automatic retry.");}return SettingsResult::None;}
            if(key==27){panel_.visible=false;reveal_=acknowledged_=false;return SettingsResult::Close;}
            if(key=='r'||key=='R'){reveal_=!reveal_;acknowledged_=false;offset_=0;originalEnd_=currentEnd_=false;}
            else if(key=='\t'){currentData_=!currentData_;offset_=0;acknowledged_=false;}
            else if(key==kKeyUp||key==kKeyPageUp){const auto step=rowBytes_*(key==kKeyUp?1:5);offset_=offset_>step?offset_-step:0;}
            else if(key==kKeyDown||key==kKeyPageDown)offset_=std::min(preferencesDocumentLimit,offset_+rowBytes_*(key==kKeyDown?1:5));
            else if(key=='i'||key=='I'){
                inspection_.reset();acknowledged_=false;originalEnd_=currentEnd_=false;offset_=0;
                admit(view_.uncertain->kind,PreferencesOperation::Load,true);
            }else if(key==' ')acknowledged_=reveal_&&inspection_&&originalEnd_&&currentEnd_?!acknowledged_:false;
            else if(key=='\r'||key=='\n'||key==kKeyF2)acknowledge();
            return SettingsResult::None;
        }
        submittedUtc_=utc;submittedMono_=mono;
        SettingsResult result=SettingsResult::None;
        try{result=panel_.key(key,
            [&](const UiPreferences &p,std::string *e){return save(PreferencesKind::Ui,uiPreferencesJson(p),e);},
            [&](const RuntimePreferences &p,std::string *e){return save(PreferencesKind::Runtime,runtimePreferencesJson(p),e);},
            [&](ManualMarketPreferences &p,std::string *e){
                if(p.enabled&&context.publicReferencesRequested){if(e)*e="Public references requested for this parent; manual activation requires a separately selected new session.";return false;}
                if(p.enabled&&!context.manualProjectionAvailable){if(e)*e="Manual reference projection unavailable in this build.";return false;}
                auto projection=context.manualFactory?context.manualFactory(p,utc,mono):ManualMarketProjector{};
                const auto result=save(PreferencesKind::Manual,manualMarketJson(p),e);
                if(ticket_)manualProjection_=std::move(projection);
                return result;
            },utc,[&](ElectricityPreferences &p,std::string *e){return save(PreferencesKind::Electricity,electricityPreferencesJson(p),e);},
            [&](const JsonValue &p,std::string *e){return save(PreferencesKind::Tariff,tariffJson(p)+"\n",e);},
            [&](const GamePreferences &p,std::string *e){return save(PreferencesKind::Game,gamePreferencesJson(p),e);},
            [&](const PoolSecurityPreferences &p,std::string *e){return save(PreferencesKind::Pool,poolSecurityPreferencesJson(p),e);},
            [&](const LanguagePreferences &p,std::string *e){return save(PreferencesKind::Language,languagePreferencesJson(p),e);});}
        catch(...){notice("Preference action unavailable; no confirmed setting applied. Any already-admitted original remains pending.");}
        switch(result){
            case SettingsResult::OpenRuntime:load(PreferencesKind::Runtime);break;
            case SettingsResult::OpenManualMarket:load(PreferencesKind::Manual);break;
            case SettingsResult::OpenElectricity:load(PreferencesKind::Electricity);break;
            case SettingsResult::OpenTariff:load(PreferencesKind::Tariff);break;
            case SettingsResult::OpenGame:load(PreferencesKind::Game);break;
            case SettingsResult::OpenPoolSecurity:load(PreferencesKind::Pool);break;
            case SettingsResult::OpenLanguage:load(PreferencesKind::Language);break;
            default:break;
        }
        return result;
    }
    std::shared_ptr<PreferencesSettingsCompletion> poll(){
        if(owner_.foreignCaller())return {};
        view_=owner_.snapshot();
        if(view_.state==PreferencesOwnerState::Faulted||view_.state==PreferencesOwnerState::Stopped){
            if(ticket_){ticket_=0;receipt_.reset();notice(view_.uncertain?"UNKNOWN preference write; original retained. No retry. Inspect after workers stop if this owner is unavailable.":"Preference helper unavailable; no current setting applied. Reopen explicitly after workers stop.");}
            completion_.reset();return {};
        }
        if(completion_){auto result=completion_;completion_.reset();return result;}
        if(!ticket_)return {};
        if(!receipt_)receipt_=owner_.takeReply(ticket_);
        if(!receipt_)return {};
        try{
            const auto status=receipt_->status;
            if(status==PreferencesReplyStatus::Loaded&&inspecting_){inspection_=receipt_;ticket_=0;receipt_.reset();view_=owner_.snapshot();return {};}
            if(status==PreferencesReplyStatus::Loaded||status==PreferencesReplyStatus::Saved){
                SettingsPanel next=panel_;if(!apply(next,*receipt_,pendingContext_,privacyRuntime_))return failReply();
                auto result=std::make_shared<PreferencesSettingsCompletion>();result->reply=receipt_;result->submittedUtc=submittedUtc_;result->submittedMono=submittedMono_;
                if(receipt_->kind==PreferencesKind::Manual&&status==PreferencesReplyStatus::Saved)result->manualValue=next.manual.active;
                const auto current=owner_.snapshot();if(current.state==PreferencesOwnerState::Stopped||current.state==PreferencesOwnerState::Faulted)return {};
                if(receipt_->kind==PreferencesKind::Manual&&status==PreferencesReplyStatus::Saved)result->manualProjection=std::move(manualProjection_);
                panel_=std::move(next);ticket_=0;receipt_.reset();view_=current;return result;
            }
            notice(status==PreferencesReplyStatus::Unknown?"UNKNOWN preference write; not Saved. Original retained; explicit inspection required.":"Preference operation refused; no confirmed save. Draft retained; explicitly reopen before another Save.");
            ticket_=0;receipt_.reset();view_=owner_.snapshot();return {};
        }catch(...){notice("Preference completion unavailable; retained receipt and draft are not a confirmed current setting.");return {};}
    }
    bool overlay()const{return ticket_||static_cast<bool>(view_.uncertain);}
    bool pending()const{return ticket_!=0;}
    std::string render(int columns,int rows,ColorLevel color)const{
        if(owner_.foreignCaller())return "Preferences unavailable for this caller.\n";
        using namespace dash_detail;const size_t width=static_cast<size_t>(std::max(20,columns)-1);
        if(rows>0&&(rows<12||(columns<40&&rows<20))){originalEnd_=currentEnd_=false;return "Preferences\nNOT Saved\nResize for review\nQ/^C Stop\nEsc Cancel/Back\n";}
        std::vector<std::string> header,footer,body;
        header.push_back(ticket_?"Preferences PENDING - NOT Saved":"Preferences UNKNOWN - NOT Saved");
        if(ticket_){header.push_back("One immutable request; edits/navigation held. Worker control continues.");footer={"Q/^C Stop; Esc Cancel"};}
        else{
            header.push_back(reveal_?"PRIVATE local review; do not share terminal captures.":"Private documents hidden. R reveals locally.");
            header.push_back("Original retained in this session only; stopping creates no recovery journal.");
            if(view_.state==PreferencesOwnerState::Faulted||view_.state==PreferencesOwnerState::Stopped)header.push_back("Owner unavailable: no inspection/restart here. Inspect explicitly after workers stop.");
            header.push_back("Inspect both complete documents; Space + Enter uses CURRENT as new baseline. No retry; old outcome remains UNKNOWN.");
            header.push_back(std::string(currentData_?"CURRENT PARSED VALUES":"ORIGINAL REQUEST")+(inspection_?" | inspected":" | inspection required"));
            footer={"Q/^C Stop; Esc Back","I Inspect; R Reveal","Tab Data; Arrows Move",std::string(acknowledged_?"[x] ":"[ ] ")+"Space Ack; Enter Use"};
            const auto document=currentData_?(inspection_?&inspection_->document:nullptr):(view_.uncertain?&view_.uncertain->document:nullptr);
            if(reveal_&&document){
                rowBytes_=std::max(size_t(1),width/4);const size_t start=std::min(offset_,document->empty()?size_t(0):((document->size()-1)/rowBytes_)*rowBytes_);
                header.push_back("Bytes "+std::to_string(start)+" / "+std::to_string(document->size())+"; reversible escapes, no truncation.");
                const auto h=wrapped(header,width),f=wrapped(footer,width);
                const size_t budget=rows>0?static_cast<size_t>(std::max(1,rows-static_cast<int>(h.size()+f.size()))):32;
                size_t end=start;for(size_t n=0;n<budget&&end<document->size();++n){const size_t through=std::min(document->size(),end+rowBytes_);body.push_back(escaped(*document,end,through));end=through;}
                if(end==document->size()){if(currentData_)currentEnd_=true;else originalEnd_=true;}
            }else body.push_back("No private bytes shown.");
        }
        auto h=wrapped(header,width),f=wrapped(footer,width);const size_t maximum=rows>0?static_cast<size_t>(rows):1000;
        // Keep stop/back controls visible even in a narrow terminal. Missing data space never establishes end-of-review.
        if(h.size()+f.size()>=maximum){h.resize(std::min(h.size(),maximum>f.size()+1?maximum-f.size()-1:size_t(1)));body.clear();originalEnd_=currentEnd_=false;}
        const size_t budget=maximum>h.size()+f.size()?maximum-h.size()-f.size():0;if(body.size()>budget)body.resize(budget);
        std::string out;for(const auto &line:h)out+=paint(line,Tone::Canary,color)+"\n";for(const auto &line:body)out+=line+"\n";for(const auto &line:f)out+=line+"\n";return out;
    }
  private:
    PreferencesOwner &owner_;SettingsPanel &panel_;PreferencesOwnerSnapshot view_;
    PreferencesSettingsContext pendingContext_;uint64_t ticket_=0,submittedUtc_=0,submittedMono_=0;
    bool inspecting_=false,privacyRuntime_=false,reveal_=false,currentData_=false,acknowledged_=false;
    size_t offset_=0;mutable size_t rowBytes_=1;mutable bool originalEnd_=false,currentEnd_=false;
    std::shared_ptr<const PreferencesReply> receipt_,inspection_;
    std::shared_ptr<PreferencesSettingsCompletion> completion_;ManualMarketProjector manualProjection_;
    static std::vector<std::string> wrapped(const std::vector<std::string> &lines,size_t width){std::vector<std::string> out;for(const auto &line:lines)for(const auto &part:dash_detail::wrapText(line,width))out.push_back(part);return out;}
    static std::string escaped(const std::string &text,size_t from,size_t through){
        static const char hex[]="0123456789abcdef";std::string out;
        for(size_t i=from;i<through;++i){const auto c=static_cast<unsigned char>(text[i]);if(c>=32&&c<=126&&c!='\\')out+=static_cast<char>(c);else{out+="\\x";out+=hex[c>>4];out+=hex[c&15];}}return out;
    }
    void notice(std::string_view text)noexcept{try{const auto set=[&](std::string &target){target.assign(text.data(),text.size());};set(panel_.message);if(panel_.runtime.visible)set(panel_.runtime.message);if(panel_.manual.visible)set(panel_.manual.message);if(panel_.electricity.visible)set(panel_.electricity.message);if(panel_.tariff.visible)set(panel_.tariff.message);if(panel_.game.visible)set(panel_.game.message);if(panel_.poolSecurity.visible)set(panel_.poolSecurity.message);if(panel_.language.visible)set(panel_.language.message);}catch(...) {}}
    bool admit(PreferencesKind kind,PreferencesOperation operation,bool inspecting,const std::string &document={},uint64_t review=0){
        try{
            auto frozen=context; // allocate before admission; a failed copy must not lose an issued operation's context
            const auto result=operation==PreferencesOperation::Load?owner_.load(kind):owner_.save(kind,review,document);
            if(result.status!=PreferencesAdmission::Queued){notice("Preference request unavailable; no operation admitted. Wait for readiness or explicitly reopen after workers stop.");return false;}
            pendingContext_=std::move(frozen);ticket_=result.ticket;inspecting_=inspecting;privacyRuntime_=panel_.group==8;receipt_.reset();view_=owner_.snapshot();notice("Preference operation QUEUED; NOT Saved. No duplicate request or automatic retry.");return true;
        }catch(...){notice("Preference admission unavailable; draft retained without confirmed Save.");return false;}
    }
    bool save(PreferencesKind kind,const std::string &document,std::string *error){
        const auto snapshot=owner_.snapshot();const bool queued=admit(kind,PreferencesOperation::Save,false,document,snapshot.reviews[static_cast<unsigned>(kind)]);
        if(error)*error=queued?"Preference Save QUEUED; NOT Saved. Original time/value frozen; wait for matching acknowledgement.":"Preference Save not admitted; draft retained. Explicitly reopen if review is unavailable.";
        return false; // async admission is never the synchronous panel's confirmed Save
    }
    std::shared_ptr<PreferencesSettingsCompletion> failReply(){owner_.requestStop();ticket_=0;receipt_.reset();notice("Preference document refused; owner stopped. No current setting applied.");return {};}
    void acknowledge(){
        if(!acknowledged_||!reveal_||!inspection_||!originalEnd_||!currentEnd_)return;
        try{
            SettingsPanel next=panel_;if(!apply(next,*inspection_,context,privacyRuntime_))return;
            auto result=std::make_shared<PreferencesSettingsCompletion>();result->reply=inspection_;
            if(!owner_.acknowledgeInspection(inspection_->kind,inspection_->review))return;
            panel_=std::move(next);completion_=std::move(result);inspection_.reset();view_=owner_.snapshot();reveal_=acknowledged_=originalEnd_=currentEnd_=false;offset_=0;
            notice("Current file accepted as a new editable baseline; historical write outcome remains UNKNOWN. No retry or deletion.");
        }catch(...){notice("Inspection acknowledgement unavailable; original and block preserved.");}
    }
    static bool apply(SettingsPanel &panel,const PreferencesReply &reply,const PreferencesSettingsContext &context,bool privacyRuntime){
        const bool saved=reply.status==PreferencesReplyStatus::Saved;
        switch(reply.kind){
            case PreferencesKind::Ui:{UiPreferences p;if(!parseUiPreferences(reply.document,&p,nullptr))return false;if(saved){panel.active=panel.draft=p;panel.page=SettingsPage::Browse;panel.message=i18n::settingsEnglish(i18n::SettingsMessageKey::Saved);}else panel.open(p);break;}
            case PreferencesKind::Runtime:{RuntimePreferences p;if(!parseRuntimePreferences(reply.document,&p,nullptr))return false;if(saved){panel.runtime.active=panel.runtime.draft=p;panel.runtime.page=RuntimeSettingsPage::Browse;panel.runtime.message=i18n::runtimePanelEnglish(i18n::RuntimePanelMessageKey::Saved);}else{panel.runtime.open(p,context.runtime);if(privacyRuntime)panel.runtime.field=1;}break;}
            case PreferencesKind::Manual:{ManualMarketPreferences p;if(!parseManualMarket(reply.document,&p,nullptr))return false;if(saved){panel.manual.active=panel.manual.draft=p;if(panel.manual.context.running&&!panel.manual.context.cliDisabled)panel.manual.context.effective=p;panel.manual.page=ManualMarketSettingsPage::Browse;panel.manual.message=i18n::manualMarketNoticeEnglish(panel.manual.context.cliDisabled?i18n::ManualMarketNoticeMessageKey::SavedCliDisabled:i18n::ManualMarketNoticeMessageKey::Saved);}else panel.manual.open(p,context.manual);break;}
            case PreferencesKind::Electricity:{ElectricityPreferences p;if(!parseElectricityPreferences(reply.document,&p,nullptr))return false;if(saved){panel.electricity.active=panel.electricity.draft=p;panel.electricity.page=ElectricitySettingsPage::Browse;panel.electricity.message=i18n::electricityNoticeEnglish(i18n::ElectricityNoticeMessageKey::Saved);}else panel.electricity.open(p,context.electricity);break;}
            case PreferencesKind::Game:{GamePreferences p;if(!parseGamePreferences(reply.document,&p,nullptr))return false;if(saved){panel.game.active=panel.game.draft=p;panel.game.page=GameSettingsPage::Browse;panel.game.message=i18n::gamePanelEnglish(i18n::GamePanelMessageKey::Saved);}else panel.game.open(std::move(p),context.game);break;}
            case PreferencesKind::Pool:{PoolSecurityPreferences p;if(!parsePoolSecurityPreferences(reply.document,&p,nullptr))return false;if(saved){panel.poolSecurity.active=panel.poolSecurity.draft=p;panel.poolSecurity.page=PoolSecuritySettingsPage::Browse;panel.poolSecurity.message=i18n::poolNoticeEnglish(i18n::PoolNoticeMessageKey::Saved);}else panel.poolSecurity.open(std::move(p),context.pool);break;}
            case PreferencesKind::Language:{LanguagePreferences p;if(!parseLanguagePreferences(reply.document,&p,nullptr))return false;if(saved){panel.language.active=panel.language.draft=p;panel.language.page=LanguageSettingsPage::Browse;panel.language.message=i18n::languagePanelEnglish(i18n::LanguagePanelMessageKey::SavedRequests);}else panel.language.open(std::move(p),context.language);break;}
            case PreferencesKind::Tariff:{JsonValue p;if(!parseTariffDeclaration(reply.document,&p,nullptr)){if(saved||reply.document!=tariffJson(tariffDeclarationTemplate())+"\n")return false;p=tariffDeclarationTemplate();}if(saved){panel.tariff.active=panel.tariff.draft=std::move(p);panel.tariff.page=TariffSettingsPage::Browse;panel.tariff.message=i18n::tariffNoticeEnglish(i18n::TariffNoticeMessageKey::SavedOffline);}else panel.tariff.open(std::move(p));break;}
            default:return false;
        }
        return true;
    }
};
} // namespace om
