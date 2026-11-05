#pragma once
#include "dashboard.h"
#include "manual_market_preferences.h"
#include "manual_market_field_messages.h"
#include "manual_market_notice_messages.h"
#include "manual_market_panel_messages.h"
#include "manual_market_value_messages.h"
#include "manual_market_context_messages.h"
namespace om {
enum class ManualMarketSettingsResult { None, Close, Quit };
enum class ManualMarketSettingsPage { Browse, Edit, Review, Leave };
struct ManualMarketContext { bool running=false, cliDisabled=false; ManualMarketPreferences effective; };
class ManualMarketSettingsPanel {
   public:
    using Save=std::function<bool(ManualMarketPreferences &,std::string *)>;
    bool visible=false, replaceInput=true;
    unsigned field=0, reviewOffset=0;
    ManualMarketPreferences active,draft;
    ManualMarketContext context;
    ManualMarketSettingsPage page=ManualMarketSettingsPage::Browse;
    std::string input,message;
    void open(const ManualMarketPreferences &p,ManualMarketContext c) {active=draft=p; context=std::move(c); visible=true; field=reviewOffset=0; page=ManualMarketSettingsPage::Browse; message.clear();}
    bool dirty() const {return active!=draft;}
    static bool integer(const std::string &text,uint64_t *out) {
        if(text.empty() || text.size()>12 || (text.size()>1 && text[0]=='0'))return false;
        uint64_t n=0; for(char c:text){if(c<'0'||c>'9')return false; n=n*10+static_cast<unsigned>(c-'0');}
        if(n>kManualLastUtc)return false;
        *out=n; return true;
    }
    ManualMarketSettingsResult key(int key,const Save &save,uint64_t nowUtc) {
        using NoticeKey=i18n::ManualMarketNoticeMessageKey;
        if(key==3)return ManualMarketSettingsResult::Quit;
        if(page==ManualMarketSettingsPage::Edit) {
            if(key==27){page=ManualMarketSettingsPage::Browse; message.clear();}
            else if(key=='\r'||key=='\n') {
                std::string value; uint64_t number=0; bool ok=false;
                if(field==1 || field==4) {
                    ok=input.empty() || canonicalManualPrice(input,&value);
                    if(ok){(field==1?draft.prlUsdt:draft.usdtUsd)=value; if(value.empty())(field==1?draft.prlAsOfUtc:draft.fxAsOfUtc)=0;}
                    else message=i18n::manualMarketNoticeEnglish(NoticeKey::InvalidDecimal);
                } else {
                    ok=integer(input,&number) && (field!=6 || (number>=60 && number<=86400)) && (field!=7 || number<=3600);
                    if(ok){if(field==2)draft.prlAsOfUtc=number; else if(field==5)draft.fxAsOfUtc=number; else if(field==6)draft.validitySeconds=static_cast<unsigned>(number); else draft.maxFxSkewSeconds=static_cast<unsigned>(number);}
                    else message=i18n::manualMarketNoticeEnglish(field==6?NoticeKey::InvalidValidity:field==7?NoticeKey::InvalidSkew:NoticeKey::InvalidReference);
                }
                if(ok){page=ManualMarketSettingsPage::Browse; message.clear();}
            } else if(key==8||key==127){if(replaceInput)input.clear(); else if(!input.empty())input.pop_back(); replaceInput=false;}
            else if(key>=32&&key<=126){if(replaceInput)input.clear(); replaceInput=false; if(input.size()<64)input+=static_cast<char>(key);}
            return ManualMarketSettingsResult::None; // Q/D/L/S are literal input, not global actions
        }
        if(key=='q'||key=='Q')return ManualMarketSettingsResult::Quit;
        if(page==ManualMarketSettingsPage::Leave) {
            if(key=='d'||key=='D'){draft=active; visible=false; return ManualMarketSettingsResult::Close;}
            if(key==27)page=ManualMarketSettingsPage::Browse;
            else if(key=='\r'||key=='\n'||key==kKeyF2)page=ManualMarketSettingsPage::Review;
        } else if(page==ManualMarketSettingsPage::Review) {
            if(key==27)page=ManualMarketSettingsPage::Browse;
            else if(key==kKeyUp||key==kKeyPageUp)reviewOffset=reviewOffset?reviewOffset-1:0;
            else if(key==kKeyDown||key==kKeyPageDown)reviewOffset=std::min(100u,reviewOffset+1);
            else if(key=='\r'||key=='\n'||key==kKeyF2) {
                auto saved=draft; std::string error;
                if(!confirmManualMarket(&saved,nowUtc,&error)){message=error; focusProblem(nowUtc);}
                else if(save && save(saved,&error)) {
                    active=draft=saved; if(context.running&&!context.cliDisabled)context.effective=saved;
                    page=ManualMarketSettingsPage::Browse; message=i18n::manualMarketNoticeEnglish(context.cliDisabled?NoticeKey::SavedCliDisabled:NoticeKey::Saved);
                } else message=error.empty()?i18n::manualMarketNoticeEnglish(NoticeKey::SaveFailed):error;
            }
        } else if(key==27) {
            if(dirty())page=ManualMarketSettingsPage::Leave; else {visible=false; return ManualMarketSettingsResult::Close;}
        } else if(key=='v'||key=='V'||key==kKeyF2){page=ManualMarketSettingsPage::Review; reviewOffset=0;}
        else if(key==kKeyDown||key=='\t')field=(field+1)%8;
        else if(key==kKeyUp||key==kKeyBackTab)field=(field+7)%8;
        else if((key=='n'||key=='N')&&(field==2||field==5)) {
            if(!nowUtc||nowUtc>kManualLastUtc)message=i18n::manualMarketNoticeEnglish(NoticeKey::ClockUnavailable);
            else {(field==2?draft.prlAsOfUtc:draft.fxAsOfUtc)=nowUtc; message=i18n::manualMarketNoticeEnglish(NoticeKey::StampedNow);}
        } else if(key==' '||key=='\r'||key=='\n') {
            message.clear();
            if(field==0)draft.enabled=!draft.enabled;
            else if(field==3)draft.fxEnabled=!draft.fxEnabled;
            else {input=field==1?draft.prlUsdt:field==4?draft.usdtUsd:field==2?std::to_string(draft.prlAsOfUtc):field==5?std::to_string(draft.fxAsOfUtc):field==6?std::to_string(draft.validitySeconds):std::to_string(draft.maxFxSkewSeconds); replaceInput=true; page=ManualMarketSettingsPage::Edit;}
        }
        return ManualMarketSettingsResult::None;
    }
    std::string render(int columns,int rows,ColorLevel color) const {
        using namespace dash_detail;
        using PanelKey=i18n::ManualMarketPanelMessageKey;
        using ValueKey=i18n::ManualMarketValueMessageKey;
        using ContextKey=i18n::ManualMarketContextMessageKey;
        const size_t width=static_cast<size_t>(std::max(20,columns)-1);
        if(rows>0&&rows<12)return i18n::manualMarketPanelEnglish(PanelKey::ShortTitle)+"\n"+i18n::manualMarketPanelEnglish(PanelKey::ShortResize)+"\n"+i18n::manualMarketPanelEnglish(PanelKey::ShortKeys)+"\n";
        const bool compact=width<70 || (rows>0&&rows<20);
        const std::string footer=i18n::manualMarketPanelEnglish(page==ManualMarketSettingsPage::Edit?PanelKey::FooterEdit:
            page==ManualMarketSettingsPage::Review?PanelKey::FooterReview:PanelKey::FooterBrowse);
        const auto footerLines=wrapText(footer,width);
        const auto messageLines=wrapText(message.empty()?i18n::manualMarketPanelEnglish(PanelKey::LocalDefaults):message,width);
        const size_t budget=rows>0?static_cast<size_t>(std::max(1,rows-static_cast<int>(footerLines.size()+messageLines.size())-1)):1000;
        std::vector<std::string> body={i18n::manualMarketPanelEnglish(PanelKey::Title)+(dirty()?i18n::manualMarketPanelEnglish(PanelKey::UnsavedSuffix):std::string{})};
        if(page==ManualMarketSettingsPage::Edit) {
            body.push_back(i18n::manualMarketValueEnglish(ValueKey::EditHeading,{{"field",label(field)}})); body.push_back(i18n::manualMarketValueEnglish(ValueKey::EditInput,{{"input",input}}));
            body.push_back(i18n::manualMarketPanelEnglish(field==1||field==4?PanelKey::DecimalGuide:field==6?PanelKey::ValidityGuide:field==7?PanelKey::SkewGuide:PanelKey::ReferenceGuide));
            body.push_back(i18n::manualMarketPanelEnglish(PanelKey::EditorKeys));
        } else if(page==ManualMarketSettingsPage::Leave) {body.push_back(i18n::manualMarketPanelEnglish(PanelKey::LeaveTitle)); body.push_back(i18n::manualMarketPanelEnglish(PanelKey::LeaveKeys));}
        else if(page==ManualMarketSettingsPage::Review) {
            body.push_back(i18n::manualMarketPanelEnglish(PanelKey::ReviewHeading));
            for(unsigned i=0;i<8;++i)if(value(active,i)!=value(draft,i))body.push_back(fieldChange(active,draft,i));
            if(!dirty())body.push_back(i18n::manualMarketPanelEnglish(PanelKey::NoChanges));
            body.push_back(i18n::manualMarketPanelEnglish(context.running?PanelKey::ActivationLive:PanelKey::ActivationNext));
            if(context.cliDisabled)body.push_back(i18n::manualMarketPanelEnglish(PanelKey::CliWins));
            body.push_back(i18n::manualMarketPanelEnglish(PanelKey::NoFinance));
            body.push_back(i18n::manualMarketPanelEnglish(PanelKey::LocalOnly));
        } else {
            const size_t available=budget>1?std::min(size_t{8},budget-1):1;
            const size_t start=compact?field:0; // selected field precedes any wrapping/truncation in a short terminal
            for(size_t i=start;i<8&&i<start+available;++i)body.push_back(std::string(i==field?"> ":"  ")+fieldValue(draft,static_cast<unsigned>(i)));
            if(!compact){
                body.push_back(i18n::manualMarketPanelEnglish(PanelKey::Identity));
                body.push_back(context.cliDisabled?i18n::manualMarketPanelEnglish(PanelKey::CurrentOff):context.running?i18n::manualMarketContextEnglish(ContextKey::CurrentDisplay,{{"state",on(context.effective.enabled)}}):i18n::manualMarketPanelEnglish(PanelKey::NoSession));
                if(context.running&&!context.cliDisabled&&context.effective!=active) {
                    body.push_back(currentPrl(context.effective));
                    body.push_back(i18n::manualMarketPanelEnglish(PanelKey::ConfiguredBoundary));
                }
                body.push_back(i18n::manualMarketPanelEnglish(PanelKey::NoProvider));
                if(field==2||field==5)body.push_back(utcLine(field==2?draft.prlAsOfUtc:draft.fxAsOfUtc));
            }
        }
        std::vector<std::string> lines;
        for(const auto &line:body)for(const auto &part:wrapText(line,width))lines.push_back(part);
        if(page==ManualMarketSettingsPage::Review && lines.size()>budget){const auto start=std::min(static_cast<size_t>(reviewOffset),lines.size()-std::min(lines.size(),budget)); lines.erase(lines.begin(),lines.begin()+start);}
        if(lines.size()>budget)lines.resize(budget);
        std::string out; for(size_t i=0;i<lines.size();++i)out+=paint(lines[i],i==0?Tone::Canary:Tone::Plain,color)+"\n";
        for(const auto &line:messageLines)out+=paint(line,Tone::Plain,color)+"\n";
        for(const auto &line:footerLines)out+=paint(line,Tone::Plain,color)+"\n";
        return out;
    }
   private:
    static std::string label(unsigned i){const auto &keys=i18n::manualMarketFieldKeys();return i18n::manualMarketFieldEnglish(i<keys.size()?keys[i]:i18n::ManualMarketFieldMessageKey::Count);}
    static std::string on(bool b){return i18n::manualMarketValueEnglish(b?i18n::ManualMarketValueMessageKey::On:i18n::ManualMarketValueMessageKey::Off);}
    static std::string stamp(uint64_t utc){return utc?utcIsoClock(static_cast<time_t>(utc)):i18n::manualMarketValueEnglish(i18n::ManualMarketValueMessageKey::NotSupplied);}
    static std::string value(const ManualMarketPreferences &p,unsigned i){return i==0?on(p.enabled):i==3?on(p.fxEnabled):i==1?(p.prlUsdt.empty()?i18n::manualMarketValueEnglish(i18n::ManualMarketValueMessageKey::NotSupplied):p.prlUsdt):i==4?(p.usdtUsd.empty()?i18n::manualMarketValueEnglish(i18n::ManualMarketValueMessageKey::NotSupplied):p.usdtUsd):i==2?std::to_string(p.prlAsOfUtc):i==5?std::to_string(p.fxAsOfUtc):i==6?std::to_string(p.validitySeconds):std::to_string(p.maxFxSkewSeconds);}
    static bool missingPrice(const ManualMarketPreferences &p,unsigned i){return (i==1&&p.prlUsdt.empty())||(i==4&&p.usdtUsd.empty());}
    static std::string fieldValue(const ManualMarketPreferences &p,unsigned i){using Key=i18n::ManualMarketValueMessageKey;
        if(missingPrice(p,i))return i18n::manualMarketValueEnglish(Key::MissingFieldValue,{{"field",label(i)}});
        return i18n::manualMarketValueEnglish(i==0||i==3?Key::HumanFieldValue:Key::FieldValue,{{"field",label(i)},{"value",value(p,i)}});
    }
    static std::string fieldChange(const ManualMarketPreferences &a,const ManualMarketPreferences &b,unsigned i){using Key=i18n::ManualMarketValueMessageKey;
        const bool beforeMissing=missingPrice(a,i),afterMissing=missingPrice(b,i);
        const auto key=i==0||i==3?Key::HumanFieldChange:beforeMissing?Key::FieldAfterOnly:afterMissing?Key::FieldBeforeOnly:Key::FieldChange;
        i18n::MessageArguments arguments={{"field",label(i)}};
        if(!beforeMissing)arguments.emplace_back("before",value(a,i));
        if(!afterMissing)arguments.emplace_back("after",value(b,i));
        return i18n::manualMarketValueEnglish(key,arguments);
    }
    static std::string currentPrl(const ManualMarketPreferences &p){using Key=i18n::ManualMarketContextMessageKey;
        const bool price=!p.prlUsdt.empty(),time=p.prlAsOfUtc!=0;
        const auto key=price?(time?Key::CurrentPrlBoth:Key::CurrentPrlPrice):(time?Key::CurrentPrlStamp:Key::CurrentPrlUnset);
        i18n::MessageArguments arguments;
        if(price)arguments.emplace_back("price",p.prlUsdt);
        if(time)arguments.emplace_back("stamp",stamp(p.prlAsOfUtc));
        return i18n::manualMarketContextEnglish(key,arguments);
    }
    static std::string utcLine(uint64_t utc){using Key=i18n::ManualMarketContextMessageKey;
        return i18n::manualMarketContextEnglish(utc?Key::UtcStamp:Key::UtcUnset,utc?i18n::MessageArguments{{"stamp",stamp(utc)}}:i18n::MessageArguments{});
    }
    void focusProblem(uint64_t now){page=ManualMarketSettingsPage::Browse; if((draft.enabled||draft.prlAsOfUtc)&&draft.prlUsdt.empty())field=1; else if(!draft.prlUsdt.empty()&&(!draft.prlAsOfUtc||(draft.enabled&&draft.prlAsOfUtc>now)))field=2; else if((draft.fxEnabled||draft.fxAsOfUtc)&&draft.usdtUsd.empty())field=4; else if(!draft.usdtUsd.empty()&&(!draft.fxAsOfUtc||(draft.enabled&&draft.fxEnabled&&draft.fxAsOfUtc>now)))field=5;}
};
}  // namespace om
