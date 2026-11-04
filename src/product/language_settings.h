// Native ASCII editor for independent presentation requests. No ICU/data loading or runtime/mining activation.
#pragma once
#include "dashboard.h"
#include "language_preferences.h"
#include <functional>
namespace om {
enum class LanguageSettingsResult { None, Close, Quit };
enum class LanguageSettingsPage { Browse, Review, Leave };
struct LanguageSettingsContext {bool running=false,cliOverrides=false;LanguagePreferences requested;};
class LanguageSettingsPanel {
   public:
    using Save=std::function<bool(const LanguagePreferences &,std::string *)>;
    bool visible=false;unsigned field=0,reviewOffset=0;LanguagePreferences active,draft;LanguageSettingsContext context;
    LanguageSettingsPage page=LanguageSettingsPage::Browse;std::string message;
    void open(LanguagePreferences value,LanguageSettingsContext current={}){active=draft=std::move(value);context=std::move(current);visible=true;field=reviewOffset=0;page=LanguageSettingsPage::Browse;message.clear();}
    bool dirty()const{return active!=draft;}
    LanguageSettingsResult key(int key,const Save &save={}){
        if(key==3||key=='q'||key=='Q')return LanguageSettingsResult::Quit;
        if(page==LanguageSettingsPage::Leave){if(key=='d'||key=='D'){draft=active;visible=false;return LanguageSettingsResult::Close;}
            if(key==27)page=LanguageSettingsPage::Browse;else if(key=='\r'||key=='\n'||key==kKeyF2)review();return LanguageSettingsResult::None;}
        if(page==LanguageSettingsPage::Review){
            if(key==27){page=LanguageSettingsPage::Browse;message.clear();}
            else if(key==kKeyUp||key==kKeyPageUp)reviewOffset=reviewOffset>(key==kKeyUp?1u:5u)?reviewOffset-(key==kKeyUp?1u:5u):0;
            else if(key==kKeyDown||key==kKeyPageDown)reviewOffset=std::min(4096u,reviewOffset+(key==kKeyDown?1u:5u));
            else if(key=='\r'||key=='\n'||key==kKeyF2){std::string error;if(!validLanguagePreferences(draft,&error)){message=error;page=LanguageSettingsPage::Browse;}
                else if(save&&save(draft,&error)){active=draft;page=LanguageSettingsPage::Browse;message="Saved requests only; effective English/legacy formatting unchanged.";}
                else message=error.empty()?"Save failed; active requests unchanged.":error;}
            return LanguageSettingsResult::None;
        }
        if(key==27){if(dirty())page=LanguageSettingsPage::Leave;else{visible=false;return LanguageSettingsResult::Close;}}
        else if(key=='v'||key=='V'||key==kKeyF2)review();
        else if(key==kKeyUp||key==kKeyBackTab)field=(field+7)%8;
        else if(key==kKeyDown||key=='\t')field=(field+1)%8;
        else if(key==' '||key=='\r'||key=='\n'||key==kKeyRight||key==kKeyLeft){change(key==kKeyLeft?-1:1);message="Draft request only; effective English presentation unchanged.";}
        return LanguageSettingsResult::None;
    }
    std::string render(int columns,int rows,ColorLevel color)const{
        using namespace dash_detail;const auto width=static_cast<std::size_t>(std::max(20,columns)-1);
        if(rows>0&&rows<12)return "Terminal too short.\nResize to 12+ rows.\n[Ctrl+C] Stop [Esc] Back\n";
        const auto footer=wrapText(page==LanguageSettingsPage::Review?"[Arrows] Scroll [Enter] Save [Esc] Edit [Q] Stop":"[Arrows] Move/change [Enter] Change [V] Review [Esc] Back [Q] Stop",width);
        const auto budget=rows>0?static_cast<std::size_t>(std::max(1,rows-static_cast<int>(footer.size())-2)):4096;
        std::vector<std::string> body;std::size_t focus=0;const auto add=[&](const std::string &text){const auto lines=wrapText(text,width);body.insert(body.end(),lines.begin(),lines.end());};
        add(std::string("Language / format requests")+(dirty()?" *":""));
        if(page==LanguageSettingsPage::Leave){add("Unsaved language requests");add("[Enter] Review [D] Discard [Esc] Continue");}
        else if(page==LanguageSettingsPage::Review){
            add("Review language-preferences.json only");add(nativeLanguageFallbackReason());
            for(unsigned i=0;i<8;++i){add(label(i));add("Before: "+value(active,i));add("After: "+value(draft,i));if(context.running||context.cliOverrides)add("Current request: "+value(context.requested,i));}
            add("Requested translation review: "+reviewStatus(draft.language));add("Only 13 common keys per pack; full UI translation is not available.");
            add(context.cliOverrides?"CLI overrides stay frozen for this parent; Save does not persist their values implicitly.":"No CLI override is inferred from navigation.");
            add("Format locale never changes language, assets, currency, timezone, electricity or mining settings.");
            add("Layout/digit policies are requests, not terminal/font qualification. ASCII recovery remains available.");
            add("Current workers, protocols, donation and machine keys stay unchanged.");add("[End language]");
        }else{
            for(unsigned i=0;i<8;++i){if(i==field)focus=body.size();add(std::string(i==field?"> ":"  ")+label(i)+": "+value(draft,i));}
            add("Effective UI: en (legacy English)");add("Effective numbers: "+std::string(effectiveNativeNumberFormat()));
            add("Requested review: "+reviewStatus(draft.language));add("All-domain native renderer unavailable; Save only stores independent requests.");
        }
        std::size_t start=0;if(body.size()>budget){if(page==LanguageSettingsPage::Review)start=std::min(static_cast<std::size_t>(reviewOffset),body.size()-budget);
            else if(page==LanguageSettingsPage::Browse&&focus>=budget)start=std::min(focus,body.size()-budget);}
        std::string result;for(auto i=start;i<body.size()&&i<start+budget;++i)result+=paint(body[i],i==0?Tone::Canary:Tone::Plain,color)+"\n";
        result+=paint(clip(message.empty()?"Effective UI: en; requested values are not active.":message,width),Tone::Plain,color)+"\n";
        for(const auto &line:footer)result+=paint(line,Tone::Plain,color)+"\n";
        return result;
    }
   private:
    static const char *label(unsigned i){static const char *labels[]={"Language ID","Format locale","Digit style","Text order","Direction","Ambiguous cells","Emoji cells","Grouping"};return i<8?labels[i]:"Invalid";}
    static std::string reviewStatus(const std::string &id){return id=="en"?"English source (not externally certified)":"machine-assisted draft (not human reviewed)";}
    static std::string value(const LanguagePreferences &p,unsigned i){switch(i){case 0:return p.language;case 1:return p.formattingLocale;case 2:return presentationDigitsName(p.digits);case 3:return presentationOrderName(p.textOrder);
        case 4:return presentationDirectionName(p.direction);case 5:return std::to_string(p.ambiguousCells);case 6:return std::to_string(p.emojiCells);default:return p.grouping?"on":"off";}}
    void change(int direction){
        if(field==0){const auto &packs=i18n::localePacks();std::size_t at=0;for(std::size_t i=0;i<packs.size();++i)if(packs[i].id==draft.language)at=i;
            draft.language=std::string(packs[(at+packs.size()+direction)%packs.size()].id);}
        else if(field==1){const auto &locales=presentation_detail::formattingLocales();std::size_t at=0;for(std::size_t i=0;i<locales.size();++i)if(draft.formattingLocale==locales[i].id)at=i;
            draft.formattingLocale=locales[(at+locales.size()+direction)%locales.size()].id;}
        else if(field==2)draft.digits=draft.digits==PresentationDigits::Latin?PresentationDigits::Locale:PresentationDigits::Latin;
        else if(field==3)draft.textOrder=static_cast<TerminalTextOrder>((static_cast<int>(draft.textOrder)+3+direction)%3);
        else if(field==4)draft.direction=static_cast<TerminalTextDirection>((static_cast<int>(draft.direction)+3+direction)%3);
        else if(field==5)draft.ambiguousCells=draft.ambiguousCells==1?2:1;
        else if(field==6)draft.emojiCells=draft.emojiCells==1?2:1;
        else if(field==7)draft.grouping=!draft.grouping;
    }
    void review(){if(validLanguagePreferences(draft,&message)){page=LanguageSettingsPage::Review;reviewOffset=0;message.clear();}}
};
} // namespace om
