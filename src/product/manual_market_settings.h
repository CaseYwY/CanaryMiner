#pragma once
#include "dashboard.h"
#include "manual_market_preferences.h"
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
        if(key==3)return ManualMarketSettingsResult::Quit;
        if(page==ManualMarketSettingsPage::Edit) {
            if(key==27){page=ManualMarketSettingsPage::Browse; message.clear();}
            else if(key=='\r'||key=='\n') {
                std::string value; uint64_t number=0; bool ok=false;
                if(field==1 || field==4) {
                    ok=input.empty() || canonicalManualPrice(input,&value);
                    if(ok){(field==1?draft.prlUsdt:draft.usdtUsd)=value; if(value.empty())(field==1?draft.prlAsOfUtc:draft.fxAsOfUtc)=0;}
                    else message="Enter a positive decimal: <=38 digits / <=18 decimals; no exponent or symbol.";
                } else {
                    ok=integer(input,&number) && (field!=6 || (number>=60 && number<=86400)) && (field!=7 || number<=3600);
                    if(ok){if(field==2)draft.prlAsOfUtc=number; else if(field==5)draft.fxAsOfUtc=number; else if(field==6)draft.validitySeconds=static_cast<unsigned>(number); else draft.maxFxSkewSeconds=static_cast<unsigned>(number);}
                    else message=field==6?"Validity: integer 60..86400 seconds; no automatic refresh.":field==7?"Maximum FX reference-time skew: integer 0..3600 seconds.":"Enter UTC Unix seconds, not milliseconds/local time; [Esc] cancels.";
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
                    page=ManualMarketSettingsPage::Browse; message=context.cliDisabled?"Saved; --no-market still suppresses this session's display.":"Saved. Manual reference display only; no miner restart or network request.";
                } else message=error.empty()?"Save failed; draft retained and active display unchanged.":error;
            }
        } else if(key==27) {
            if(dirty())page=ManualMarketSettingsPage::Leave; else {visible=false; return ManualMarketSettingsResult::Close;}
        } else if(key=='v'||key=='V'||key==kKeyF2){page=ManualMarketSettingsPage::Review; reviewOffset=0;}
        else if(key==kKeyDown||key=='\t')field=(field+1)%8;
        else if(key==kKeyUp||key==kKeyBackTab)field=(field+7)%8;
        else if((key=='n'||key=='N')&&(field==2||field==5)) {
            if(!nowUtc||nowUtc>kManualLastUtc)message="UTC clock unavailable; reference time unchanged.";
            else {(field==2?draft.prlAsOfUtc:draft.fxAsOfUtc)=nowUtc; message="Reference stamped NOW by explicit action; use only for a reference from now.";}
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
        const size_t width=static_cast<size_t>(std::max(20,columns)-1);
        if(rows>0&&rows<12)return "Terminal too short.\nResize to 12+ rows.\n[Ctrl+C] Stop [Esc] Back\n";
        const bool compact=width<70 || (rows>0&&rows<20);
        const std::string footer=page==ManualMarketSettingsPage::Edit?"[Enter] Apply draft [Esc] Cancel edit [Ctrl+C] Stop":
            page==ManualMarketSettingsPage::Review?"[Enter/F2] Save [Arrows] Scroll [Esc] Edit [Q] Stop":"[Tab/Arrows] Field [Enter/Space] Change [N] Stamp UTC [V/F2] Review [Esc] Back [Q] Stop";
        const auto footerLines=wrapText(footer,width);
        const auto messageLines=wrapText(message.empty()?"Manual input only; stored confirmation is NOT a quote timestamp.":message,width);
        const size_t budget=rows>0?static_cast<size_t>(std::max(1,rows-static_cast<int>(footerLines.size()+messageLines.size())-1)):1000;
        std::vector<std::string> body={"Manual/offline market"+std::string(dirty()?" *":"")};
        if(page==ManualMarketSettingsPage::Edit) {
            body.push_back(std::string("Edit ")+label(field)); body.push_back("> "+input+"_");
            body.push_back(field==1||field==4?"Positive base-10 decimal; empty clears this pair.":field==6?"Validity in seconds (60..86400), not refresh cadence.":field==7?"Maximum separation of PRL/FX reference times (0..3600 seconds).":"Reference UTC Unix seconds. [N] from browse explicitly stamps now.");
            body.push_back("Q/D/L/S are text here. Ctrl+C still stops.");
        } else if(page==ManualMarketSettingsPage::Leave) {body.push_back("Unsaved offline-market settings"); body.push_back("[Enter] Review/save [D] Discard [Esc] Continue editing");}
        else if(page==ManualMarketSettingsPage::Review) {
            body.push_back("Review manual-market-preferences.json only");
            for(unsigned i=0;i<8;++i)if(value(active,i)!=value(draft,i))body.push_back(std::string(label(i))+": "+value(active,i)+" -> "+value(draft,i));
            if(!dirty())body.push_back("No value changes; saving does not refresh reference times.");
            body.push_back(context.running?"Activation: live terminal references only; no restart.":"Activation: next explicit dashboard session; no mining was started.");
            if(context.cliDisabled)body.push_back("Current CLI --no-market wins over saved preferences.");
            body.push_back("No HTTPS, trading, actual income, forecasts or economic control.");
            body.push_back("Prices/FX stay local; UI/runtime drafts and donation are untouched.");
        } else {
            const size_t available=budget>1?std::min(size_t{8},budget-1):1;
            const size_t start=compact?field:0; // selected field precedes any wrapping/truncation in a short terminal
            for(size_t i=start;i<8&&i<start+available;++i)body.push_back(std::string(i==field?"> ":"  ")+label(static_cast<unsigned>(i))+": "+value(draft,static_cast<unsigned>(i)));
            if(!compact){
                body.push_back("Identity: Pearl mainnet native PRL; quote USDT; FX USD per USDT.");
                body.push_back(context.cliDisabled?"Current display: OFF (--no-market CLI override)":context.running?std::string("Current display: ")+on(context.effective.enabled):"No active mining session; local settings only.");
                if(context.running&&!context.cliDisabled&&context.effective!=active) {
                    body.push_back("Current PRL input: "+(context.effective.prlUsdt.empty()?"not supplied":context.effective.prlUsdt)+" USDT/PRL | "+stamp(context.effective.prlAsOfUtc));
                    body.push_back("Fields above are configured values; current inputs change only after successful Save.");
                }
                body.push_back("No provider fetch or 1h average. Manual references are not income.");
                if(field==2||field==5)body.push_back("UTC: "+stamp(field==2?draft.prlAsOfUtc:draft.fxAsOfUtc)+" | N explicitly stamps NOW.");
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
    static const char *label(unsigned i){static const char *names[]={"Display enabled","PRL price (USDT/PRL)","PRL reference UTC seconds","FX enabled","FX price (USD/USDT)","FX reference UTC seconds","Validity seconds","Maximum FX skew seconds"}; return names[i];}
    static std::string on(bool b){return b?"on":"off";}
    static std::string stamp(uint64_t utc){return utc?utcIsoClock(static_cast<time_t>(utc)):"not supplied";}
    static std::string value(const ManualMarketPreferences &p,unsigned i){return i==0?on(p.enabled):i==3?on(p.fxEnabled):i==1?(p.prlUsdt.empty()?"not supplied":p.prlUsdt):i==4?(p.usdtUsd.empty()?"not supplied":p.usdtUsd):i==2?std::to_string(p.prlAsOfUtc):i==5?std::to_string(p.fxAsOfUtc):i==6?std::to_string(p.validitySeconds):std::to_string(p.maxFxSkewSeconds);}
    void focusProblem(uint64_t now){page=ManualMarketSettingsPage::Browse; if((draft.enabled||draft.prlAsOfUtc)&&draft.prlUsdt.empty())field=1; else if(!draft.prlUsdt.empty()&&(!draft.prlAsOfUtc||(draft.enabled&&draft.prlAsOfUtc>now)))field=2; else if((draft.fxEnabled||draft.fxAsOfUtc)&&draft.usdtUsd.empty())field=4; else if(!draft.usdtUsd.empty()&&(!draft.fxAsOfUtc||(draft.enabled&&draft.fxEnabled&&draft.fxAsOfUtc>now)))field=5;}
};
}  // namespace om
