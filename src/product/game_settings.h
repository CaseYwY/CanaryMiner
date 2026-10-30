// Native private game-default editor. Separate save target; no detection, metrics or mining actions.
#pragma once
#include <functional>
#include "dashboard.h"
#include "game_preferences.h"
namespace om {
struct GameSettingsContext {bool running=false;std::optional<GamePreferences> effective;};
enum class GameSettingsResult { None, Close, Quit };
enum class GameSettingsPage { Browse, Edit, Review, Leave };
class GameSettingsPanel {
   public:
    using Save=std::function<bool(const GamePreferences &,std::string *)>;
    bool visible=false,replaceInput=true;unsigned field=0,reviewOffset=0;
    GameSettingsPage page=GameSettingsPage::Browse;GamePreferences active,draft;GameSettingsContext context;
    std::string input,message;
    void open(GamePreferences value,GameSettingsContext current){active=draft=std::move(value);context=std::move(current);visible=true;
        field=reviewOffset=0;page=GameSettingsPage::Browse;message.clear();input.clear();}
    bool dirty()const{return active!=draft;}
    GameSettingsResult key(int key,const Save &save={}){
        if(key==3)return GameSettingsResult::Quit;
        if(page==GameSettingsPage::Edit){
            if(key==27){page=GameSettingsPage::Browse;message.clear();}
            else if(key=='\r'||key=='\n'){
                auto value=draft;bool ok=true;
                if(field==1){ok=input.empty()||validGameProcessName(input);if(ok)value.executable=input;}
                else{unsigned number=0;ok=!input.empty()&&input.size()<=5;
                    for(char c:input){if(c<'0'||c>'9'){ok=false;break;}number=number*10+static_cast<unsigned>(c-'0');}
                    ok=ok&&number<=(field==3?10000u:30000u)&&(field==4||number>=100);
                    if(ok){if(field==3)value.pollMs=number;else if(field==4)value.exitDebounceMs=number;else value.maximumDetectionAgeMs=number;}}
                if(ok){draft=std::move(value);page=GameSettingsPage::Browse;message.clear();}
                else message=field==1?"Use 1..128 ASCII basename characters, not a path or pattern.":field==4?"Enter an integer 0..30000 ms.":field==3?"Enter an integer 100..10000 ms.":"Enter an integer 100..30000 ms.";
            }else if(key==8||key==127){if(replaceInput)input.clear();else if(!input.empty())input.pop_back();replaceInput=false;}
            else if(key>=32&&key<=126){if(replaceInput)input.clear();replaceInput=false;if(input.size()<(field==1?128u:8u))input+=static_cast<char>(key);}
            return GameSettingsResult::None;
        }
        if(key=='q'||key=='Q')return GameSettingsResult::Quit;
        if(page==GameSettingsPage::Leave){
            if(key=='d'||key=='D'){draft=active;visible=false;return GameSettingsResult::Close;}
            if(key==27)page=GameSettingsPage::Browse;
            else if(key=='\r'||key=='\n'||key==kKeyF2)review();
        }else if(page==GameSettingsPage::Review){
            if(key==27){page=GameSettingsPage::Browse;message.clear();}
            else if(key==kKeyUp||key==kKeyPageUp)reviewOffset=reviewOffset>(key==kKeyUp?1u:5u)?reviewOffset-(key==kKeyUp?1u:5u):0;
            else if(key==kKeyDown||key==kKeyPageDown)reviewOffset=(std::min)(1000u,reviewOffset+(key==kKeyDown?1u:5u));
            else if(key=='\r'||key=='\n'||key==kKeyF2){if(!validGamePreferences(draft,&message,&field)){page=GameSettingsPage::Browse;return GameSettingsResult::None;}
                std::string error;if(save&&save(draft,&error)){active=draft;page=GameSettingsPage::Browse;message="Saved game defaults; no runtime activation.";}
                else message=error.empty()?"Save failed; draft retained.":error;}
        }else if(key==27){if(dirty())page=GameSettingsPage::Leave;else{visible=false;return GameSettingsResult::Close;}}
        else if(key=='v'||key=='V'||key==kKeyF2)review();
        else if(key==kKeyUp||key==kKeyBackTab)field=(field+5)%6;
        else if(key==kKeyDown||key=='\t')field=(field+1)%6;
        else if(key==' '||key=='\r'||key=='\n'){
            message.clear();
            if(field==0)draft.mode=draft.mode==GameIntent::Off?GameIntent::Pause:draft.mode==GameIntent::Pause?GameIntent::Coexist:GameIntent::Off;
            else if(field==2)draft.adapter=draft.adapter==GameProcessAdapter::WindowsSessionImage?GameProcessAdapter::LinuxUidNativeImage:GameProcessAdapter::WindowsSessionImage;
            else{input=value(draft,field);replaceInput=true;page=GameSettingsPage::Edit;}
        }
        return GameSettingsResult::None;
    }
    std::string render(int columns,int rows,ColorLevel color)const{
        using namespace dash_detail;const size_t width=static_cast<size_t>((std::max)(20,columns)-1);
        if(rows>0&&rows<12)return "Terminal too short.\nResize to 12+ rows.\n[Ctrl+C] Stop [Esc] Back\n";
        const auto footer=wrapText(page==GameSettingsPage::Edit?"[Enter] Draft [Esc] Cancel [Ctrl+C] Stop":
            page==GameSettingsPage::Review?"[Arrows] Scroll [Enter] Save [Esc] Edit [Q] Stop":"[Tab] Field [Enter] Change [V] Review [Esc] Back [Q] Stop",width);
        const size_t budget=rows>0?static_cast<size_t>((std::max)(1,rows-static_cast<int>(footer.size())-2)):1000;
        std::vector<std::string> body;size_t focus=0;
        auto add=[&](const std::string &text){const auto lines=wrapText(text,width);body.insert(body.end(),lines.begin(),lines.end());};
        add(std::string("Games / saved defaults")+(dirty()?" *":""));
        if(page==GameSettingsPage::Edit){add(label(field)+":");
            const auto tail=input.size()>width-3?input.substr(input.size()-(width-3)):input;add("> "+tail+"_");
            add("Q/D/L/S are input. Esc cancels this field.");
        }else if(page==GameSettingsPage::Leave){add("Unsaved game defaults");add("[Enter] Review [D] Discard [Esc] Continue");}
        else if(page==GameSettingsPage::Review){
            add("Review game-preferences.json only");add("Next explicit parent start only.");
            add("Native safety/CLI binding unavailable.");add("A non-off request refuses startup.");
            for(unsigned i=0;i<6;++i)add(label(i)+": "+value(active,i)+" -> "+value(draft,i));
            if(context.running){
                add(context.effective?"Current session (frozen):":"Current session: effective defaults unavailable.");
                if(context.effective)for(unsigned i=0;i<6;++i)add(label(i)+": "+value(*context.effective,i));
            }else add("No active session.");
            add("Scope is explicit; foreign platforms cannot imply game exit. Unknown detection holds work.");
            add("Coexist requires real baseline/experience and economic qualification. Ten-minute nonpositive-profit lock remains on.");
            add("Current session, its workers and game episode remain frozen. No scan or game starts from Save.");
            add("[End of game defaults]");
        }else{
            add(context.running?(context.effective?"Effective: "+std::string(gameModeName(context.effective->mode))+" (frozen)":"Effective: unavailable"):"No active session");
            for(unsigned i=0;i<6;++i){if(i==field)focus=body.size();add(std::string(i==field?"> ":"  ")+label(i)+": "+value(draft,i));}
            add("Configured values only. Native safety/CLI activation unavailable; non-off refuses the next ordinary start.");
            add("Exact basename and scope; incomplete detection stays unknown. Coexist still requires qualification.");
        }
        size_t start=0;if(body.size()>budget){
            if(page==GameSettingsPage::Review)start=(std::min)(static_cast<size_t>(reviewOffset),body.size()-budget);
            else if(page==GameSettingsPage::Browse&&focus>=budget)start=focus;
        }
        std::string text;for(size_t i=start;i<body.size()&&i<start+budget;++i)text+=paint(body[i],i==0?Tone::Canary:Tone::Plain,color)+"\n";
        text+=paint(clip(message.empty()?"Private defaults; no live activation.":message,width),Tone::Plain,color)+"\n";
        for(const auto &line:footer)text+=paint(line,Tone::Plain,color)+"\n";
        return text;
    }
   private:
    void review(){if(validGamePreferences(draft,&message,&field)){page=GameSettingsPage::Review;reviewOffset=0;message.clear();}else page=GameSettingsPage::Browse;}
    static std::string label(unsigned field){static const char *names[]={"Mode","Target","Scope","Poll ms","Exit debounce ms","Max age ms"};return field<6?names[field]:"Invalid";}
    static std::string value(const GamePreferences &v,unsigned field){switch(field){
        case 0:return gameModeName(v.mode);case 1:return v.executable;case 2:return v.adapter==GameProcessAdapter::WindowsSessionImage?"Windows session":"Linux native UID";
        case 3:return std::to_string(v.pollMs);case 4:return std::to_string(v.exitDebounceMs);default:return std::to_string(v.maximumDetectionAgeMs);}}
};
} // namespace om
