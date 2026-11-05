// Native explicit review/save of independent declarations. No network or credential/private-material controls.
#pragma once
#include "dashboard.h"
#include "pool_security_preferences.h"
#include "pool_field_messages.h"
#include "pool_notice_messages.h"
#include <functional>
namespace om {
enum class PoolSecuritySettingsResult { None, Close, Quit };
enum class PoolSecuritySettingsPage { Browse, Edit, Review, Leave };
struct PoolSecuritySettingsContext {bool running=false;};
class PoolSecuritySettingsPanel {
   public:
    using Save=std::function<bool(const PoolSecurityPreferences &,std::string *)>;
    bool visible=false,donation=false,replaceInput=true;unsigned slot=0,field=0,reviewOffset=0;
    PoolSecurityPreferences active,draft;PoolSecuritySettingsContext context;
    PoolSecuritySettingsPage page=PoolSecuritySettingsPage::Browse;std::string input,message;
    void open(PoolSecurityPreferences value,PoolSecuritySettingsContext current={}){active=draft=std::move(value);context=current;visible=true;donation=false;slot=field=reviewOffset=0;page=PoolSecuritySettingsPage::Browse;message.clear();input.clear();}
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
        if(rows>0&&rows<12)return "Terminal too short.\nResize to 12+ rows.\n[Ctrl+C] Stop [Esc] Back\n";
        const auto footer=wrapText(page==PoolSecuritySettingsPage::Edit?"[Enter] Draft [Esc] Cancel [Ctrl+C] Stop":page==PoolSecuritySettingsPage::Review?
            "[Arrows] Scroll [Enter] Save [Esc] Edit [Q] Stop":"[Tab] Field [Enter] Change [V] Review [Esc] Back [Q] Stop",width);
        const auto budget=rows>0?static_cast<std::size_t>(std::max(1,rows-static_cast<int>(footer.size())-2)):4096;
        std::vector<std::string> body;std::size_t focus=0;auto add=[&](const std::string &s){const auto lines=wrapText(s,width);body.insert(body.end(),lines.begin(),lines.end());};
        add(page==PoolSecuritySettingsPage::Edit&&field==4?"PLAINTEXT RISK":std::string("Pools / TLS declarations")+(dirty()?" *":""));
        if(page==PoolSecuritySettingsPage::Edit){
            add(field==4?"Login/proofs exposed. MITM can trigger it.":field==6?"Remove selected user declaration only; no live pool changes.":"Explicit DNS endpoint only; no password, wallet, path or inferred port.");
            add(field==4?"Type ALLOW PLAINTEXT":field==6?"Type REMOVE POOL.":"Q/D/L/S are input; Esc cancels.");
            const auto tail=input.size()>width-3?input.substr(input.size()-(width-3)):input;add("> "+tail+"_");
            focus=body.size()>budget?body.size()-budget:0;
        }else if(page==PoolSecuritySettingsPage::Leave){add("Unsaved pool declarations");add("[Enter] Review [D] Discard [Esc] Continue");}
        else if(page==PoolSecuritySettingsPage::Review){
            add("Review pool-security-preferences.json only");add("No live activation; requested TLS refuses the next ordinary mining start until the runtime is qualified.");
            add("Certificate, hostname, trust, unknown and protocol failures NEVER downgrade. Explicit strict SSL URLs never fall back.");
            add("Plaintext exposes login and proofs. Network-error fallback is NOT downgrade resistant.");
            for(std::size_t i=0;i<std::max(active.users.size(),draft.users.size());++i){add("User pool "+std::to_string(i+1));if(i<active.users.size())add("Before: "+describe(active.users[i]));else add("Before: absent");
                if(i<draft.users.size())add("After: "+describe(draft.users[i]));else add("After: removed");}
            add("Donation - independent scope; no inherited endpoint or consent");add("Before: "+describe(active.donation));add("After: "+describe(draft.donation));
            add(context.running?"Current session and its connections stay unchanged.":"No active session; Save starts no networking.");
            add("No published candidate endpoint is treated as verified by this declaration. F2Pool/donation TLS must not be guessed.");add("[End of TLS review]");
        }else{
            const auto &rule=selected();for(unsigned i=0;i<7;++i){if(i==field)focus=body.size();add(std::string(i==field?"> ":"  ")+label(i)+": "+value(rule,i));}
            add("Configured only; TLS runtime unavailable. User and donation scopes never inherit permissions.");
        }
        std::size_t start=0;if(body.size()>budget){if(page==PoolSecuritySettingsPage::Review)start=std::min(static_cast<std::size_t>(reviewOffset),body.size()-budget);
            else if(page==PoolSecuritySettingsPage::Edit)start=focus;else if(page==PoolSecuritySettingsPage::Browse&&focus>=budget)start=std::min(focus,body.size()-budget);}
        std::string result;for(std::size_t i=start;i<body.size()&&i<start+budget;++i)result+=paint(body[i],i==0?Tone::Canary:Tone::Plain,color)+"\n";
        result+=paint(clip(message.empty()?"Declarations only; no live connection.":message,width),Tone::Plain,color)+"\n";
        for(const auto &line:footer)result+=paint(line,Tone::Plain,color)+"\n";
        return result;
    }
   private:
    PoolSecurityRule &selected(){return donation?draft.donation:draft.users[slot];}
    const PoolSecurityRule &selected()const{return donation?draft.donation:draft.users[slot];}
    static std::string describe(const PoolSecurityRule &r){return std::string(poolSecurityModeName(r.mode))+" | TLS: "+(r.tlsUrl.empty()?"unset":r.tlsUrl)+" | TCP: "+(r.plaintextUrl.empty()?"unset":r.plaintextUrl)+" | plaintext consent: "+(r.plaintextConsent?"YES":"off");}
    static std::string label(unsigned n){const auto &keys=i18n::poolFieldKeys();return i18n::poolFieldEnglish(n<keys.size()?keys[n]:i18n::PoolFieldMessageKey::Invalid);}
    std::string value(const PoolSecurityRule &r,unsigned n)const{switch(n){case 0:return donation?"Donation (independent)":"User pool "+std::to_string(slot+1);case 1:return poolSecurityModeName(r.mode);
        case 2:return r.tlsUrl.empty()?"unset":r.tlsUrl;case 3:return r.plaintextUrl.empty()?"unset":r.plaintextUrl;case 4:return r.plaintextConsent?"YES (Enter revokes)":"off (explicit phrase required)";case 5:return "[Enter] strict TLS, no consent";default:return "[Enter] explicit confirmation";}}
    bool validate(){if(draft.users.empty()||draft.users.size()>8){message=i18n::poolNoticeEnglish(i18n::PoolNoticeMessageKey::InvalidUserCount);return false;}
        unsigned bad=0;for(std::size_t i=0;i<draft.users.size();++i)if(!validPoolSecurityRule(draft.users[i],&message,&bad)){donation=false;slot=static_cast<unsigned>(i);field=bad+1;return false;}
        if(!validPoolSecurityRule(draft.donation,&message,&bad)){donation=true;field=bad+1;return false;}return true;}
    void review(){if(validate()){page=PoolSecuritySettingsPage::Review;reviewOffset=0;message.clear();}else page=PoolSecuritySettingsPage::Browse;}
};
} // namespace om
