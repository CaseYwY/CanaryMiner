#pragma once
#include "dashboard.h"
#include "electricity_preferences.h"
#include "electricity_field_messages.h"
#include "electricity_notice_messages.h"
#include "electricity_panel_messages.h"
#include "electricity_value_messages.h"
#include <functional>
namespace om {
enum class ElectricitySettingsResult { None, Close, Quit };
enum class ElectricitySettingsPage { Browse, Edit, Review, Leave };
struct ElectricitySettingsContext {bool running=false,cliNoEnergy=false;ElectricityPreferences effective;};
class ElectricitySettingsPanel {
   public:
    using Save=std::function<bool(ElectricityPreferences &,std::string *)>;
    bool visible=false,replaceInput=true,inputRejected=false;unsigned field=0,reviewOffset=0;
    ElectricityPreferences active,draft;ElectricitySettingsContext context;
    ElectricitySettingsPage page=ElectricitySettingsPage::Browse;std::string input,message;
    void open(ElectricityPreferences p,ElectricitySettingsContext c){active=draft=std::move(p);context=std::move(c);visible=true;field=reviewOffset=0;page=ElectricitySettingsPage::Browse;message.clear();}
    bool dirty()const{return active!=draft;}
    static bool unsignedNumber(const std::string &s,uint64_t *value){
        if(s.empty()||s.size()>12||(s.size()>1&&s.front()=='0'))return false;
        uint64_t n=0;for(char c:s){if(c<'0'||c>'9')return false;n=n*10+static_cast<unsigned>(c-'0');}if(n>253402300799ULL)return false;*value=n;return true;
    }
    ElectricitySettingsResult key(int key,const Save &save,uint64_t now){
        using NoticeKey=i18n::ElectricityNoticeMessageKey;
        if(key==3)return ElectricitySettingsResult::Quit;
        if(page==ElectricitySettingsPage::Edit){
            if(key==27){page=ElectricitySettingsPage::Browse;message.clear();}
            else if(key=='\r'||key=='\n'){if(applyInput()){page=ElectricitySettingsPage::Browse;message.clear();}}
            else if(key==8||key==127){if(replaceInput)input.clear();else if(!input.empty())input.pop_back();replaceInput=false;inputRejected=false;}
            else if(key>=32&&key<=126){if(replaceInput){input.clear();inputRejected=false;}replaceInput=false;if(input.size()<64)input+=static_cast<char>(key);else{inputRejected=true;message=i18n::electricityNoticeEnglish(NoticeKey::InputTooLong);}}
            else if(key>=128&&key<=255){inputRejected=true;message=i18n::electricityNoticeEnglish(NoticeKey::NonAscii);}
            return ElectricitySettingsResult::None;
        }
        if(key=='q'||key=='Q')return ElectricitySettingsResult::Quit;
        if(page==ElectricitySettingsPage::Leave){
            if(key=='d'||key=='D'){draft=active;visible=false;return ElectricitySettingsResult::Close;}
            if(key==27)page=ElectricitySettingsPage::Browse;
            else if(key=='\r'||key=='\n'||key==kKeyF2)page=ElectricitySettingsPage::Review;
        }else if(page==ElectricitySettingsPage::Review){
            if(key==27)page=ElectricitySettingsPage::Browse;
            else if(key==kKeyUp||key==kKeyPageUp)reviewOffset=reviewOffset?reviewOffset-1:0;
            else if(key==kKeyDown||key==kKeyPageDown)reviewOffset=std::min(256u,reviewOffset+1);
            else if(key=='\r'||key=='\n'||key==kKeyF2){
                auto candidate=draft;std::string error;
                if(!confirmElectricityPreferences(&candidate,now,&error)){message=error;focusProblem();}
                else if(save&&save(candidate,&error)){active=draft=std::move(candidate);page=ElectricitySettingsPage::Browse;message=i18n::electricityNoticeEnglish(NoticeKey::Saved);}
                else message=error.empty()?i18n::electricityNoticeEnglish(NoticeKey::SaveFailed):error;
            }
        }else if(key==27){if(dirty())page=ElectricitySettingsPage::Leave;else{visible=false;return ElectricitySettingsResult::Close;}}
        else if(key=='v'||key=='V'||key==kKeyF2){page=ElectricitySettingsPage::Review;reviewOffset=0;}
        else if(key==kKeyDown||key=='\t')field=(field+1)%17;
        else if(key==kKeyUp||key==kKeyBackTab)field=(field+16)%17;
        else if((key=='n'||key=='N')&&field==15){if(now&&now<=253402300799ULL){draft.effectiveFromUtc=now;message=i18n::electricityNoticeEnglish(NoticeKey::StampedNow);}}
        else if(key==' '||key=='\r'||key=='\n'){
            message.clear();
            if(field==0)draft.collectEnergy=!draft.collectEnergy;else if(field==1)draft.showEnergy=!draft.showEnergy;
            else if(field==4)draft.tariffEnabled=!draft.tariffEnabled;else if(field==5)draft.exclusionsAcknowledged=!draft.exclusionsAcknowledged;
            else if(field==13){draft.fixedStandardTime=!draft.fixedStandardTime;if(!draft.fixedStandardTime)draft.fixedOffsetSeconds=0;}
            else{input=value(draft,field,false);replaceInput=true;inputRejected=false;page=ElectricitySettingsPage::Edit;}
        }
        return ElectricitySettingsResult::None;
    }
    std::string render(int columns,int rows,ColorLevel color)const{
        using namespace dash_detail;const size_t width=static_cast<size_t>(std::max(20,columns)-1);
        using PanelKey=i18n::ElectricityPanelMessageKey;
        using ValueKey=i18n::ElectricityValueMessageKey;
        if(rows>0&&rows<12)return i18n::electricityPanelEnglish(PanelKey::ShortTitle)+"\n"+i18n::electricityPanelEnglish(PanelKey::ShortResize)+"\n"+i18n::electricityPanelEnglish(PanelKey::ShortKeys)+"\n";
        const auto footer=wrapText(i18n::electricityPanelEnglish(page==ElectricitySettingsPage::Edit?PanelKey::FooterEdit:page==ElectricitySettingsPage::Review?PanelKey::FooterReview:PanelKey::FooterBrowse),width);
        const auto notice=wrapText(message.empty()?i18n::electricityPanelEnglish(PanelKey::LocalDefaults):message,width);
        const size_t budget=rows>0?static_cast<size_t>(std::max(1,rows-static_cast<int>(footer.size()+notice.size())-1)):1000;
        std::vector<std::string> body={i18n::electricityPanelEnglish(PanelKey::Title)+(dirty()?i18n::electricityPanelEnglish(PanelKey::UnsavedSuffix):std::string{})};
        if(page==ElectricitySettingsPage::Edit){
            body.push_back(i18n::electricityValueEnglish(ValueKey::EditHeading,{{"field",label(field)}}));body.push_back(i18n::electricityValueEnglish(ValueKey::EditInput,{{"input",input}}));
            body.push_back(i18n::electricityPanelEnglish(field==12?PanelKey::RateGuide:field>=6&&field<=11?PanelKey::IdentityGuide:field==14?PanelKey::OffsetGuide:field>=15?PanelKey::UtcGuide:PanelKey::IntervalGuide));
            body.push_back(i18n::electricityPanelEnglish(PanelKey::EditorKeys));
        }else if(page==ElectricitySettingsPage::Review){
            body.push_back(i18n::electricityPanelEnglish(PanelKey::ReviewHeading));
            for(unsigned i=0;i<17;++i)if(value(active,i)!=value(draft,i))body.push_back(fieldChange(active,draft,i));
            if(!dirty())body.push_back(i18n::electricityPanelEnglish(PanelKey::NoChanges));
            body.push_back(i18n::electricityPanelEnglish(PanelKey::Activation));
            body.push_back("Model/basis: flat ENERGY-ONLY / GPU BOARD proxy; clock "+value(draft,13)+", offset "+value(draft,14)+" seconds.");
            body.push_back("Declared effective UTC: "+(draft.effectiveFromUtc?utcIsoClock(static_cast<time_t>(draft.effectiveFromUtc)):"not supplied")+" to "+(draft.effectiveThroughUtc?utcIsoClock(static_cast<time_t>(draft.effectiveThroughUtc)):"not supplied")+" (end exclusive).");
            if(context.cliNoEnergy)body.push_back(i18n::electricityPanelEnglish(PanelKey::CliWins));
            body.push_back("Current session collection: "+std::string(context.running&&context.effective.collectEnergy&&!context.cliNoEnergy?"on":"off/no active session"));
            body.push_back(currentTariff());
            body.push_back(i18n::electricityPanelEnglish(PanelKey::SeparateDrafts));
            body.push_back(i18n::electricityPanelEnglish(PanelKey::Exclusions));
            body.push_back(i18n::electricityPanelEnglish(PanelKey::NoPermission));
        }else if(page==ElectricitySettingsPage::Leave){body.push_back(i18n::electricityPanelEnglish(PanelKey::LeaveTitle));body.push_back(i18n::electricityPanelEnglish(PanelKey::LeaveKeys));}
        else{
            body.push_back("Field "+std::to_string(field+1)+"/17 | configured next-session defaults");
            for(unsigned i=field;i<17&&i<field+7;++i)body.push_back(std::string(i==field?"> ":"  ")+fieldValue(draft,i));
            body.push_back("Current collection: "+std::string(context.cliNoEnergy?"off (--no-energy)":context.running&&context.effective.collectEnergy?"on (frozen session defaults)":"off/no active session"));
            body.push_back(currentTariff());
            body.push_back(i18n::electricityPanelEnglish(PanelKey::NoInference));
        }
        std::vector<std::string> lines;for(const auto &line:body)for(const auto &part:wrapText(line,width))lines.push_back(part);
        if(page==ElectricitySettingsPage::Review&&lines.size()>budget){const auto offset=std::min(static_cast<size_t>(reviewOffset),lines.size()-std::min(budget,lines.size()));lines.erase(lines.begin(),lines.begin()+offset);}
        if(lines.size()>budget)lines.resize(budget);
        std::string text;for(size_t i=0;i<lines.size();++i)text+=paint(lines[i],i?Tone::Plain:Tone::Canary,color)+"\n";
        for(const auto &line:notice)text+=line+"\n";
        for(const auto &line:footer)text+=line+"\n";
        return text;
    }
   private:
    std::string currentTariff()const{
        const auto &p=context.effective;
        return "Current tariff: "+(context.running&&p.collectEnergy&&p.tariffEnabled&&!context.cliNoEnergy?p.ratePerKwh+" "+p.currency+"/kWh | "+p.version:std::string("off/no active collection"));
    }
    static std::string label(unsigned f){const auto &keys=i18n::electricityFieldKeys();return i18n::electricityFieldEnglish(f<keys.size()?keys[f]:i18n::ElectricityFieldMessageKey::Count);}
    static std::string value(const ElectricityPreferences &p,unsigned f,bool display=true){
        using Key=i18n::ElectricityValueMessageKey;
        std::string text;
        if(f==0||f==1||f==4||f==5)return i18n::electricityValueEnglish((f==0?p.collectEnergy:f==1?p.showEnergy:f==4?p.tariffEnabled:p.exclusionsAcknowledged)?Key::On:Key::Off);
        if(f==2)return std::to_string(p.recordIntervalMs);
        if(f==3)return std::to_string(p.maximumGapMs);
        if(f>=6&&f<=12){const std::string *values[]={&p.country,&p.region,&p.supplier,&p.product,&p.version,&p.currency,&p.ratePerKwh};text=*values[f-6];return display&&text.empty()?i18n::electricityValueEnglish(Key::NotSupplied):text;}
        if(f==13)return i18n::electricityValueEnglish(p.fixedStandardTime?Key::FixedClock:Key::ExplicitUtc);
        if(f==14)return std::to_string(p.fixedOffsetSeconds);
        return std::to_string(f==15?p.effectiveFromUtc:p.effectiveThroughUtc);
    }
    static bool humanField(unsigned f){return f==0||f==1||f==4||f==5||f==13;}
    static bool missingDeclaration(const ElectricityPreferences &p,unsigned f){return f>=6&&f<=12&&value(p,f,false).empty();}
    static std::string fieldValue(const ElectricityPreferences &p,unsigned f){using Key=i18n::ElectricityValueMessageKey;
        if(missingDeclaration(p,f))return i18n::electricityValueEnglish(Key::MissingFieldValue,{{"field",label(f)}});
        return i18n::electricityValueEnglish(humanField(f)?Key::HumanFieldValue:Key::FieldValue,{{"field",label(f)},{"value",value(p,f)}});
    }
    static std::string fieldChange(const ElectricityPreferences &a,const ElectricityPreferences &b,unsigned f){using Key=i18n::ElectricityValueMessageKey;
        const bool beforeMissing=missingDeclaration(a,f),afterMissing=missingDeclaration(b,f);
        const auto key=humanField(f)?Key::HumanFieldChange:beforeMissing?Key::FieldAfterOnly:afterMissing?Key::FieldBeforeOnly:Key::FieldChange;
        i18n::MessageArguments arguments={{"field",label(f)}};
        if(!beforeMissing)arguments.emplace_back("before",value(a,f));
        if(!afterMissing)arguments.emplace_back("after",value(b,f));
        return i18n::electricityValueEnglish(key,arguments);
    }
    bool applyInput(){
        using NoticeKey=i18n::ElectricityNoticeMessageKey;
        if(inputRejected){message=i18n::electricityNoticeEnglish(NoticeKey::RejectedInput);return false;}
        if(field>=6&&field<=11){
            if(!electricityLabel(input)){message=i18n::electricityNoticeEnglish(NoticeKey::InvalidLabel);return false;}
            if(field==11&&!input.empty()&&(input.size()!=3||input.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ")!=std::string::npos)){message=i18n::electricityNoticeEnglish(NoticeKey::InvalidCurrency);return false;}
            std::string *values[]={&draft.country,&draft.region,&draft.supplier,&draft.product,&draft.version,&draft.currency};*values[field-6]=input;return true;
        }
        if(field==12){std::string normalized;if(!input.empty()&&!canonicalElectricityRate(input,&normalized)){message=i18n::electricityNoticeEnglish(NoticeKey::InvalidRate);return false;}draft.ratePerKwh=normalized;return true;}
        uint64_t number=0;const bool negative=field==14&&!input.empty()&&input.front()=='-';const auto digits=negative?input.substr(1):input;
        if(!unsignedNumber(digits,&number)){message=i18n::electricityNoticeEnglish(NoticeKey::InvalidInteger);return false;}
        if(field==2||field==3){if(number<500||number>60000){message=i18n::electricityNoticeEnglish(NoticeKey::InvalidInterval);return false;}(field==2?draft.recordIntervalMs:draft.maximumGapMs)=static_cast<unsigned>(number);}
        else if(field==14){if(number>64800||number%60||(!draft.fixedStandardTime&&number)){message=i18n::electricityNoticeEnglish(NoticeKey::InvalidOffset);return false;}draft.fixedOffsetSeconds=static_cast<int>(number)*(negative?-1:1);}
        else (field==15?draft.effectiveFromUtc:draft.effectiveThroughUtc)=number;
        return true;
    }
    void focusProblem(){page=ElectricitySettingsPage::Browse;if(draft.maximumGapMs<draft.recordIntervalMs){field=3;return;}if(draft.tariffEnabled){if(!draft.exclusionsAcknowledged){field=5;return;}for(unsigned i=6;i<=12;++i)if(value(draft,i,false).empty()){field=i;return;}if(!draft.effectiveFromUtc||draft.effectiveThroughUtc<=draft.effectiveFromUtc)field=15;}}
};
} // namespace om
