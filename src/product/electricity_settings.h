#pragma once
#include "dashboard.h"
#include "electricity_preferences.h"
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
        if(key==3)return ElectricitySettingsResult::Quit;
        if(page==ElectricitySettingsPage::Edit){
            if(key==27){page=ElectricitySettingsPage::Browse;message.clear();}
            else if(key=='\r'||key=='\n'){if(applyInput()){page=ElectricitySettingsPage::Browse;message.clear();}}
            else if(key==8||key==127){if(replaceInput)input.clear();else if(!input.empty())input.pop_back();replaceInput=false;inputRejected=false;}
            else if(key>=32&&key<=126){if(replaceInput){input.clear();inputRejected=false;}replaceInput=false;if(input.size()<64)input+=static_cast<char>(key);else{inputRejected=true;message="Input exceeds 64 characters; nothing applied. Edit shorter or cancel.";}}
            else if(key>=128&&key<=255){inputRejected=true;message="This editor currently requires ASCII; non-ASCII input was not applied. Cancel or edit again.";}
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
                else if(save&&save(candidate,&error)){active=draft=std::move(candidate);page=ElectricitySettingsPage::Browse;message="Saved for NEXT EXPLICIT SESSION ONLY; current collection/tariff unchanged. No miner restart.";}
                else message=error.empty()?"Save failed; draft retained and current session unchanged.":error;
            }
        }else if(key==27){if(dirty())page=ElectricitySettingsPage::Leave;else{visible=false;return ElectricitySettingsResult::Close;}}
        else if(key=='v'||key=='V'||key==kKeyF2){page=ElectricitySettingsPage::Review;reviewOffset=0;}
        else if(key==kKeyDown||key=='\t')field=(field+1)%17;
        else if(key==kKeyUp||key==kKeyBackTab)field=(field+16)%17;
        else if((key=='n'||key=='N')&&field==15){if(now&&now<=253402300799ULL){draft.effectiveFromUtc=now;message="Effective start explicitly stamped now; this is a user declaration, not a supplier timestamp.";}}
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
        if(rows>0&&rows<12)return "Terminal too short.\nResize to 12+ rows.\n[Ctrl+C] Stop [Esc] Back\n";
        const auto footer=wrapText(page==ElectricitySettingsPage::Edit?"[Enter] Apply draft [Esc] Cancel edit [Ctrl+C] Stop":page==ElectricitySettingsPage::Review?"[Enter/F2] Save next-session defaults [Arrows] Scroll [Esc] Edit [Q] Stop":"[Tab/Arrows] Field [Enter/Space] Change [N] Start UTC now [V/F2] Review [Esc] Back [Q] Stop",width);
        const auto notice=wrapText(message.empty()?"GPU BOARD / SESSION ONLY. Flat ENERGY-ONLY is not a utility bill or net income.":message,width);
        const size_t budget=rows>0?static_cast<size_t>(std::max(1,rows-static_cast<int>(footer.size()+notice.size())-1)):1000;
        std::vector<std::string> body={"Electricity/energy"+std::string(dirty()?" *":"")};
        if(page==ElectricitySettingsPage::Edit){
            body.push_back(std::string("Edit ")+label(field));body.push_back("> "+input+"_");
            body.push_back(field==12?"Signed decimal currency/kWh; zero valid. No exponent or implicit FX.":field>=6&&field<=11?"Printable ASCII, max 64. Do NOT enter account, wallet or credentials.":field==14?"Whole-minute fixed STANDARD seconds (+/-64800), not an IANA/DST zone.":field>=15?"UTC Unix seconds, not milliseconds or local time.":"Milliseconds; selection >=500; max gap >= selection and <=60000.");
            body.push_back("Q/D/L/S are text here, not global actions.");
        }else if(page==ElectricitySettingsPage::Review){
            body.push_back("Review electricity-preferences.json ONLY");
            for(unsigned i=0;i<17;++i)if(value(active,i)!=value(draft,i))body.push_back(std::string(label(i))+": "+value(active,i)+" -> "+value(draft,i));
            if(!dirty())body.push_back("No value changes.");
            body.push_back("Activation: NEXT EXPLICIT parent session only, never on child retry.");
            body.push_back("Model/basis: flat ENERGY-ONLY / GPU BOARD proxy; clock "+value(draft,13)+", offset "+value(draft,14)+" seconds.");
            body.push_back("Declared effective UTC: "+(draft.effectiveFromUtc?utcIsoClock(static_cast<time_t>(draft.effectiveFromUtc)):"not supplied")+" to "+(draft.effectiveThroughUtc?utcIsoClock(static_cast<time_t>(draft.effectiveThroughUtc)):"not supplied")+" (end exclusive).");
            if(context.cliNoEnergy)body.push_back("Current --no-energy overrides collection, including after Save.");
            body.push_back("Current session collection: "+std::string(context.running&&context.effective.collectEnergy&&!context.cliNoEnergy?"on":"off/no active session"));
            body.push_back(currentTariff());
            body.push_back("UI/runtime/manual-market drafts save separately. No history deletion.");
            body.push_back("Flat energy-only EXCLUDES tiers/TOU/dynamic/fees/tax/shared-meter bill effects.");
            body.push_back("No hardware changes, income/net forecast, optimizer, network or mining permission.");
        }else if(page==ElectricitySettingsPage::Leave){body.push_back("Unsaved electricity settings");body.push_back("[Enter] Review/save [D] Discard [Esc] Continue editing");}
        else{
            body.push_back("Field "+std::to_string(field+1)+"/17 | configured next-session defaults");
            for(unsigned i=field;i<17&&i<field+7;++i)body.push_back(std::string(i==field?"> ":"  ")+label(i)+": "+value(draft,i));
            body.push_back("Current collection: "+std::string(context.cliNoEnergy?"off (--no-energy)":context.running&&context.effective.collectEnergy?"on (frozen session defaults)":"off/no active session"));
            body.push_back(currentTariff());
            body.push_back("No country, supplier, tariff, currency or OS timezone inference.");
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
    static const char *label(unsigned f){static const char *names[]={"Collect session board energy","Show energy section","Record selection ms","Maximum hold gap ms","Use declared flat energy rate","Energy-only exclusions acknowledged","Country/jurisdiction","Region/territory","Supplier","Product/plan","Effective version key","Currency identifier","Flat rate per kWh","Reporting clock","Fixed standard offset seconds","Effective start UTC seconds","Effective end UTC seconds (exclusive)"};return names[f];}
    static std::string value(const ElectricityPreferences &p,unsigned f,bool display=true){
        std::string text;
        if(f==0||f==1||f==4||f==5)return (f==0?p.collectEnergy:f==1?p.showEnergy:f==4?p.tariffEnabled:p.exclusionsAcknowledged)?"on":"off";
        if(f==2)return std::to_string(p.recordIntervalMs);
        if(f==3)return std::to_string(p.maximumGapMs);
        if(f>=6&&f<=12){const std::string *values[]={&p.country,&p.region,&p.supplier,&p.product,&p.version,&p.currency,&p.ratePerKwh};text=*values[f-6];return display&&text.empty()?"not supplied":text;}
        if(f==13)return p.fixedStandardTime?"fixed STANDARD (no DST)":"explicit UTC";
        if(f==14)return std::to_string(p.fixedOffsetSeconds);
        return std::to_string(f==15?p.effectiveFromUtc:p.effectiveThroughUtc);
    }
    bool applyInput(){
        if(inputRejected){message="Input was too long or non-ASCII; nothing applied. Shorten/edit or cancel.";return false;}
        if(field>=6&&field<=11){
            if(!electricityLabel(input)){message="Use <=64 printable ASCII characters, no surrounding spaces.";return false;}
            if(field==11&&!input.empty()&&(input.size()!=3||input.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ")!=std::string::npos)){message="Currency: three UPPERCASE letters, no symbol or implicit FX.";return false;}
            std::string *values[]={&draft.country,&draft.region,&draft.supplier,&draft.product,&draft.version,&draft.currency};*values[field-6]=input;return true;
        }
        if(field==12){std::string normalized;if(!input.empty()&&!canonicalElectricityRate(input,&normalized)){message="Enter a signed decimal <=38 digits / <=18 decimals; zero valid, no exponent.";return false;}draft.ratePerKwh=normalized;return true;}
        uint64_t number=0;const bool negative=field==14&&!input.empty()&&input.front()=='-';const auto digits=negative?input.substr(1):input;
        if(!unsignedNumber(digits,&number)){message="Enter an integer in the stated units; UTC uses seconds, not milliseconds.";return false;}
        if(field==2||field==3){if(number<500||number>60000){message="Milliseconds must be 500..60000; maximum gap must also cover selection.";return false;}(field==2?draft.recordIntervalMs:draft.maximumGapMs)=static_cast<unsigned>(number);}
        else if(field==14){if(number>64800||number%60||(!draft.fixedStandardTime&&number)){message="Select fixed STANDARD clock first; offset in whole-minute seconds within +/-64800.";return false;}draft.fixedOffsetSeconds=static_cast<int>(number)*(negative?-1:1);}
        else (field==15?draft.effectiveFromUtc:draft.effectiveThroughUtc)=number;
        return true;
    }
    void focusProblem(){page=ElectricitySettingsPage::Browse;if(draft.maximumGapMs<draft.recordIntervalMs){field=3;return;}if(draft.tariffEnabled){if(!draft.exclusionsAcknowledged){field=5;return;}for(unsigned i=6;i<=12;++i)if(value(draft,i,false).empty()){field=i;return;}if(!draft.effectiveFromUtc||draft.effectiveThroughUtc<=draft.effectiveFromUtc)field=15;}}
};
} // namespace om
