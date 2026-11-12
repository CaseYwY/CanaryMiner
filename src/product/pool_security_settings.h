// Native explicit review/save of independent declarations. No network or credential/private-material controls.
#pragma once
#include "dashboard.h"
#include "pool_security_preferences.h"
#include "pool_field_messages.h"
#include "pool_notice_messages.h"
#include "pool_panel_messages.h"
#include "pool_value_messages.h"
#include "pool_rule_messages.h"
#include <functional>
namespace om {
enum class PoolSecuritySettingsResult { None, Close, Quit };
enum class PoolSecuritySettingsPage { Browse, Edit, Review, Leave };
struct PoolSecuritySettingsContext {
    bool running=false;
    PoolTransportPolicy effective=PoolTransportPolicy::Unknown;
    StatsPoolTransport current{};
    std::string frozenHost{};
};
class PoolSecuritySettingsPanel {
   public:
    using Save=std::function<bool(const PoolSecurityPreferences &,std::string *)>;
    bool visible=false,donation=false,replaceInput=true,browseScroll=false;unsigned slot=0,field=0,reviewOffset=0,browseOffset=0;
    PoolSecurityPreferences active,draft;PoolSecuritySettingsContext context;
    PoolSecuritySettingsPage page=PoolSecuritySettingsPage::Browse;std::string input,message;
    void open(PoolSecurityPreferences value,PoolSecuritySettingsContext current={}){active=draft=std::move(value);context=std::move(current);visible=true;donation=false;browseScroll=false;slot=field=reviewOffset=browseOffset=0;page=PoolSecuritySettingsPage::Browse;message.clear();input.clear();}
    bool dirty()const{return active!=draft;}
    PoolSecuritySettingsResult key(int key,const Save &save={}){
        using NoticeKey=i18n::PoolNoticeMessageKey;
        if(key==3)return PoolSecuritySettingsResult::Quit;
        if(page==PoolSecuritySettingsPage::Edit){
            if(key==27){page=PoolSecuritySettingsPage::Browse;message.clear();}
            else if(key=='\r'||key=='\n'){
                bool ok=false;auto &rule=selected();
                if(field==2||field==3){PoolSecurityEndpoint endpoint;ok=input.empty()||(parsePoolSecurityUrl(input,&endpoint)&&endpoint.tls==(field==2));
                    if(ok){(field==2?rule.tlsUrl:rule.plaintextUrl)=input.empty()?std::string{}:poolSecurityUrl(endpoint);rule.plaintextConsent=false;}}
                else if(field==4){ok=input=="ALLOW PLAINTEXT"&&rule.mode==PoolSecurityMode::PreferTls&&!rule.tlsUrl.empty()&&!rule.plaintextUrl.empty()&&validPoolSecurityRule(rule);
                    if(ok)rule.plaintextConsent=true;}
                else if(field==6){ok=input=="REMOVE POOL"&&!donation&&draft.users.size()>1;
                    if(ok){draft.users.erase(draft.users.begin()+slot);slot=std::min(slot,static_cast<unsigned>(draft.users.size()-1));}}
                if(ok){page=PoolSecuritySettingsPage::Browse;message=i18n::poolNoticeEnglish(NoticeKey::DraftChanged);}
                else message=i18n::poolNoticeEnglish(field==4?NoticeKey::InvalidConsent:field==6?NoticeKey::InvalidRemove:NoticeKey::InvalidEndpoint);
            }else if(key==8||key==127){if(replaceInput)input.clear();else if(!input.empty())input.pop_back();replaceInput=false;}
            else if(key>=32&&key<=126){if(replaceInput)input.clear();replaceInput=false;if(input.size()<300)input+=static_cast<char>(key);}
            return PoolSecuritySettingsResult::None;
        }
        if(key=='q'||key=='Q')return PoolSecuritySettingsResult::Quit;
        if(page==PoolSecuritySettingsPage::Browse&&(key==kKeyPageUp||key==kKeyPageDown)){
            if(!browseScroll){browseScroll=true;browseOffset=0;}
            else if(key==kKeyPageUp)browseOffset=browseOffset>3?browseOffset-3:0;
            else browseOffset=std::min(4096u,browseOffset+3);
            return PoolSecuritySettingsResult::None;
        }
        if(page==PoolSecuritySettingsPage::Browse&&(key==kKeyUp||key==kKeyDown||key==kKeyBackTab||key=='\t'||key==' '||key=='\r'||key=='\n'))browseScroll=false;
        if(page==PoolSecuritySettingsPage::Leave){
            if(key=='d'||key=='D'){draft=active;visible=false;return PoolSecuritySettingsResult::Close;}
            if(key==27)page=PoolSecuritySettingsPage::Browse;else if(key=='\r'||key=='\n'||key==kKeyF2)review();
        }else if(page==PoolSecuritySettingsPage::Review){
            if(key==27){page=PoolSecuritySettingsPage::Browse;message.clear();}
            else if(key==kKeyUp||key==kKeyPageUp)reviewOffset=reviewOffset>(key==kKeyUp?1u:5u)?reviewOffset-(key==kKeyUp?1u:5u):0;
            else if(key==kKeyDown||key==kKeyPageDown)reviewOffset=std::min(4096u,reviewOffset+(key==kKeyDown?1u:5u));
            else if(key=='\r'||key=='\n'||key==kKeyF2){if(!validate()){page=PoolSecuritySettingsPage::Browse;return PoolSecuritySettingsResult::None;}
                std::string error;if(save&&save(draft,&error)){active=draft;page=PoolSecuritySettingsPage::Browse;message=i18n::poolNoticeEnglish(NoticeKey::Saved);}else message=error.empty()?i18n::poolNoticeEnglish(NoticeKey::SaveFailed):error;}
        }else if(key==27){if(dirty())page=PoolSecuritySettingsPage::Leave;else{visible=false;return PoolSecuritySettingsResult::Close;}}
        else if(key=='v'||key=='V'||key==kKeyF2)review();
        else if(key==kKeyUp||key==kKeyBackTab)field=(field+6)%7;
        else if(key==kKeyDown||key=='\t')field=(field+1)%7;
        else if(key==' '||key=='\r'||key=='\n'){
            message.clear();auto &rule=selected();
            if(field==0){if(donation){donation=false;slot=0;}else if(slot+1<draft.users.size())++slot;else donation=true;}
            else if(field==1){rule.mode=rule.mode==PoolSecurityMode::Unchanged?PoolSecurityMode::StrictTls:rule.mode==PoolSecurityMode::StrictTls?PoolSecurityMode::PreferTls:PoolSecurityMode::Unchanged;rule.plaintextConsent=false;}
            else if(field==5){if(draft.users.size()>=8)message=i18n::poolNoticeEnglish(NoticeKey::AtMostEight);else{PoolSecurityRule added;added.mode=PoolSecurityMode::StrictTls;draft.users.push_back(added);donation=false;slot=static_cast<unsigned>(draft.users.size()-1);field=2;}}
            else if(field==4&&rule.plaintextConsent){rule.plaintextConsent=false;message=i18n::poolNoticeEnglish(NoticeKey::ConsentRevoked);}
            else if(field==6&&(donation||draft.users.size()==1))message=i18n::poolNoticeEnglish(NoticeKey::CannotRemove);
            else{input=field==2?rule.tlsUrl:field==3?rule.plaintextUrl:std::string{};replaceInput=true;page=PoolSecuritySettingsPage::Edit;}
        }
        return PoolSecuritySettingsResult::None;
    }
    std::string render(int columns,int rows,ColorLevel color)const{
        using namespace dash_detail;const auto width=static_cast<std::size_t>(std::max(20,columns)-1);
        using PanelKey=i18n::PoolPanelMessageKey;
        using ValueKey=i18n::PoolValueMessageKey;
        if(rows>0&&rows<12)return i18n::poolPanelEnglish(PanelKey::ShortTitle)+"\n"+i18n::poolPanelEnglish(PanelKey::ShortResize)+"\n"+i18n::poolPanelEnglish(PanelKey::ShortKeys)+"\n";
        const auto footer=wrapText(i18n::poolPanelEnglish(page==PoolSecuritySettingsPage::Edit?PanelKey::FooterEdit:page==PoolSecuritySettingsPage::Review?
            PanelKey::FooterReview:PanelKey::FooterBrowse)+(page==PoolSecuritySettingsPage::Browse?" "+i18n::poolTransportEnglish(i18n::PoolTransportMessageKey::Scroll):std::string{}),width);
        const auto budget=rows>0?static_cast<std::size_t>(std::max(1,rows-static_cast<int>(footer.size())-2)):4096;
        std::vector<std::string> body;std::size_t focus=0;auto add=[&](const std::string &s){const auto lines=wrapText(s,width);body.insert(body.end(),lines.begin(),lines.end());};
        add(page==PoolSecuritySettingsPage::Edit&&field==4?i18n::poolPanelEnglish(PanelKey::PlaintextRisk):i18n::poolPanelEnglish(PanelKey::Title)+(dirty()?i18n::poolPanelEnglish(PanelKey::UnsavedSuffix):std::string{}));
        if(page==PoolSecuritySettingsPage::Browse||page==PoolSecuritySettingsPage::Review){
            add(poolTransportText(context.running?context.current:StatsPoolTransport{}));
            add(poolTransportPolicyText(context.effective));
            if(!context.frozenHost.empty())add(i18n::poolTransportEnglish(i18n::PoolTransportMessageKey::Host,{{"host",context.frozenHost}}));
            const auto currentError=poolTransportErrorText(context.running?context.current:StatsPoolTransport{});
            if(!currentError.empty())add(currentError);
        }
        if(page==PoolSecuritySettingsPage::Edit){
            add(i18n::poolPanelEnglish(field==4?PanelKey::LoginExposed:field==6?PanelKey::RemoveOnly:PanelKey::ExplicitDns));
            add(i18n::poolPanelEnglish(field==4?PanelKey::ConsentPrompt:field==6?PanelKey::RemovePrompt:PanelKey::EditorKeys));
            const auto tail=input.size()>width-3?input.substr(input.size()-(width-3)):input;add(i18n::poolValueEnglish(ValueKey::EditInput,{{"input",tail}}));
            focus=body.size()>budget?body.size()-budget:0;
        }else if(page==PoolSecuritySettingsPage::Leave){add(i18n::poolPanelEnglish(PanelKey::LeaveTitle));add(i18n::poolPanelEnglish(PanelKey::LeaveKeys));}
        else if(page==PoolSecuritySettingsPage::Review){
            add(i18n::poolPanelEnglish(PanelKey::ReviewHeading));add(i18n::poolPanelEnglish(PanelKey::NoRuntime));
            add(i18n::poolPanelEnglish(PanelKey::NoDowngrade));
            add(i18n::poolPanelEnglish(PanelKey::PlaintextExposure));
            for(std::size_t i=0;i<std::max(active.users.size(),draft.users.size());++i){add(userPool(i+1));if(i<active.users.size())add(reviewRule(active.users[i],true));else add(i18n::poolPanelEnglish(PanelKey::BeforeAbsent));
                if(i<draft.users.size())add(reviewRule(draft.users[i],false));else add(i18n::poolPanelEnglish(PanelKey::AfterRemoved));}
            add(i18n::poolPanelEnglish(PanelKey::DonationIndependent));add(reviewRule(active.donation,true));add(reviewRule(draft.donation,false));
            add(i18n::poolPanelEnglish(context.running?PanelKey::CurrentUnchanged:PanelKey::NoSessionNetwork));
            add(i18n::poolPanelEnglish(PanelKey::EndpointNotVerified));add(i18n::poolPanelEnglish(PanelKey::EndMarker));
        }else{
            const auto &rule=selected();for(unsigned i=0;i<7;++i){if(i==field)focus=body.size();const bool machine=i==1||(i==2&&!rule.tlsUrl.empty())||(i==3&&!rule.plaintextUrl.empty());
                add(std::string(i==field?"> ":"  ")+i18n::poolValueEnglish(machine?ValueKey::FieldValue:ValueKey::HumanFieldValue,{{"field",label(i)},{"value",value(rule,i)}}));}
            add(i18n::poolPanelEnglish(PanelKey::ConfiguredOnly));
        }
        std::size_t start=0;if(body.size()>budget){if(page==PoolSecuritySettingsPage::Review)start=std::min(static_cast<std::size_t>(reviewOffset),body.size()-budget);
            else if(page==PoolSecuritySettingsPage::Edit)start=focus;else if(page==PoolSecuritySettingsPage::Browse){
                if(browseScroll)start=std::min(static_cast<std::size_t>(browseOffset),body.size()-budget);
                else if(focus>=budget)start=std::min(focus,body.size()-budget);}}
        std::string result;for(std::size_t i=start;i<body.size()&&i<start+budget;++i)result+=paint(body[i],i==0?Tone::Canary:Tone::Plain,color)+"\n";
        result+=paint(clip(message.empty()?i18n::poolPanelEnglish(PanelKey::LocalDefaults):message,width),Tone::Plain,color)+"\n";
        for(const auto &line:footer)result+=paint(line,Tone::Plain,color)+"\n";
        return result;
    }
   private:
    PoolSecurityRule &selected(){return donation?draft.donation:draft.users[slot];}
    const PoolSecurityRule &selected()const{return donation?draft.donation:draft.users[slot];}
    static std::string reviewRule(const PoolSecurityRule &r,bool before){
        using Key=i18n::PoolRuleMessageKey;const bool tls=!r.tlsUrl.empty(),tcp=!r.plaintextUrl.empty();
        const auto key=before?(tls?(tcp?Key::BeforeBoth:Key::BeforeTls):(tcp?Key::BeforeTcp:Key::BeforeUnset)):
            (tls?(tcp?Key::AfterBoth:Key::AfterTls):(tcp?Key::AfterTcp:Key::AfterUnset));
        i18n::MessageArguments arguments={{"mode",poolSecurityModeName(r.mode)},{"consent",r.plaintextConsent?"YES":"off"}};
        if(tls)arguments.emplace_back("tls",r.tlsUrl);
        if(tcp)arguments.emplace_back("tcp",r.plaintextUrl);
        return i18n::poolRuleEnglish(key,arguments);
    }
    static std::string label(unsigned n){const auto &keys=i18n::poolFieldKeys();return i18n::poolFieldEnglish(n<keys.size()?keys[n]:i18n::PoolFieldMessageKey::Invalid);}
    static std::string userPool(std::size_t n){return i18n::poolValueEnglish(i18n::PoolValueMessageKey::UserPool,{{"slot",std::to_string(n)}});}
    std::string value(const PoolSecurityRule &r,unsigned n)const{using ValueKey=i18n::PoolValueMessageKey;switch(n){case 0:return donation?i18n::poolValueEnglish(ValueKey::Donation):userPool(slot+1);case 1:return poolSecurityModeName(r.mode);
        case 2:return r.tlsUrl.empty()?i18n::poolValueEnglish(ValueKey::Unset):r.tlsUrl;case 3:return r.plaintextUrl.empty()?i18n::poolValueEnglish(ValueKey::Unset):r.plaintextUrl;case 4:return i18n::poolValueEnglish(r.plaintextConsent?ValueKey::ConsentEnabled:ValueKey::ConsentDisabled);case 5:return i18n::poolValueEnglish(ValueKey::AddAction);default:return i18n::poolValueEnglish(ValueKey::RemoveAction);}}
    bool validate(){if(draft.users.empty()||draft.users.size()>8){message=i18n::poolNoticeEnglish(i18n::PoolNoticeMessageKey::InvalidUserCount);return false;}
        unsigned bad=0;for(std::size_t i=0;i<draft.users.size();++i)if(!validPoolSecurityRule(draft.users[i],&message,&bad)){donation=false;slot=static_cast<unsigned>(i);field=bad+1;return false;}
        if(!validPoolSecurityRule(draft.donation,&message,&bad)){donation=true;field=bad+1;return false;}return true;}
    void review(){if(validate()){page=PoolSecuritySettingsPage::Review;reviewOffset=0;message.clear();}else page=PoolSecuritySettingsPage::Browse;}
};
} // namespace om
