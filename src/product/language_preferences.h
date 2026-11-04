// Independent local presentation requests. Parsing, saving and navigation never activate Unicode or mining.
#pragma once
#include "localization.h"
#include "localized_format.h"
#include "presentation_locales.h"
#include "terminal_text.h"
#include "local_preferences_store.h"
#include "../core/http.h"
#include <optional>
namespace om {
struct LanguagePreferences {
    std::string language="en",formattingLocale="en";
    PresentationDigits digits=PresentationDigits::Latin;
    TerminalTextOrder textOrder=TerminalTextOrder::Ascii;
    TerminalTextDirection direction=TerminalTextDirection::Auto;
    unsigned ambiguousCells=1,emojiCells=2;
    bool grouping=true;
    bool operator==(const LanguagePreferences &p)const{return language==p.language&&formattingLocale==p.formattingLocale&&digits==p.digits&&textOrder==p.textOrder&&direction==p.direction&&ambiguousCells==p.ambiguousCells&&emojiCells==p.emojiCells&&grouping==p.grouping;}
    bool operator!=(const LanguagePreferences &p)const{return !(*this==p);}
};
inline bool knownMessageLanguage(std::string_view id){for(const auto &pack:i18n::localePacks())if(pack.id==id)return true;return false;}
inline bool validLanguagePreferences(const LanguagePreferences &p,std::string *error=nullptr){
    const bool ok=knownMessageLanguage(p.language)&&knownPresentationFormattingLocale(p.formattingLocale)&&static_cast<unsigned>(p.digits)<=1&&
        static_cast<unsigned>(p.textOrder)<=2&&static_cast<unsigned>(p.direction)<=2&&(p.ambiguousCells==1||p.ambiguousCells==2)&&(p.emojiCells==1||p.emojiCells==2);
    if(!ok&&error)*error="Invalid independent language/format/layout request; no values applied.";
    return ok;
}
inline const char *presentationDigitsName(PresentationDigits p){return p==PresentationDigits::Latin?"latin":p==PresentationDigits::Locale?"locale_default":"invalid";}
inline const char *presentationOrderName(TerminalTextOrder p){return p==TerminalTextOrder::Ascii?"ascii":p==TerminalTextOrder::Logical?"logical":p==TerminalTextOrder::Visual?"visual":"invalid";}
inline const char *presentationDirectionName(TerminalTextDirection p){return p==TerminalTextDirection::Auto?"auto":p==TerminalTextDirection::LeftToRight?"ltr":p==TerminalTextDirection::RightToLeft?"rtl":"invalid";}
inline std::string languagePreferencesJson(const LanguagePreferences &p){return "{\"schema_version\":1,\"language\":"+jsonQuoted(p.language)+",\"formatting_locale\":"+jsonQuoted(p.formattingLocale)+
    ",\"digits\":"+jsonQuoted(presentationDigitsName(p.digits))+",\"text_order\":"+jsonQuoted(presentationOrderName(p.textOrder))+",\"direction\":"+jsonQuoted(presentationDirectionName(p.direction))+
    ",\"ambiguous_cells\":"+std::to_string(p.ambiguousCells)+",\"emoji_cells\":"+std::to_string(p.emojiCells)+",\"grouping\":"+(p.grouping?"true":"false")+"}\n";}
inline bool parseLanguagePreferences(const std::string &text,LanguagePreferences *out,std::string *error){
    const auto fail=[&](){if(error)*error="Invalid or unsupported language preferences; original preserved.";return false;};
    JsonValue root;if(!out||text.size()>4096||!parseJsonStrict(text,&root,nullptr)||!root.isObject()||root.members.size()!=9)return fail();
    const auto *schema=root.find("schema_version"),*language=root.find("language"),*format=root.find("formatting_locale"),*digits=root.find("digits"),*order=root.find("text_order"),*direction=root.find("direction"),*ambiguous=root.find("ambiguous_cells"),*emoji=root.find("emoji_cells"),*grouping=root.find("grouping");
    uint64_t version=0,wide=0,pictograph=0;
    if(!schema||!schema->asUint64(&version)||version!=1||!language||!language->isString()||!format||!format->isString()||!digits||!digits->isString()||!order||!order->isString()||!direction||!direction->isString()||
        !ambiguous||!ambiguous->asUint64(&wide)||(wide!=1&&wide!=2)||!emoji||!emoji->asUint64(&pictograph)||(pictograph!=1&&pictograph!=2)||!grouping||!grouping->isBool())return fail();
    LanguagePreferences p;p.language=language->text;p.formattingLocale=format->text;p.ambiguousCells=static_cast<unsigned>(wide);p.emojiCells=static_cast<unsigned>(pictograph);p.grouping=grouping->boolean;
    if(digits->text=="latin")p.digits=PresentationDigits::Latin;else if(digits->text=="locale_default")p.digits=PresentationDigits::Locale;else return fail();
    if(order->text=="ascii")p.textOrder=TerminalTextOrder::Ascii;else if(order->text=="logical")p.textOrder=TerminalTextOrder::Logical;else if(order->text=="visual")p.textOrder=TerminalTextOrder::Visual;else return fail();
    if(direction->text=="auto")p.direction=TerminalTextDirection::Auto;else if(direction->text=="ltr")p.direction=TerminalTextDirection::LeftToRight;else if(direction->text=="rtl")p.direction=TerminalTextDirection::RightToLeft;else return fail();
    if(!validLanguagePreferences(p,error))return false;
    *out=std::move(p);return true;
}
using LanguagePreferencesStore=LocalPreferencesStore<LanguagePreferences,parseLanguagePreferences,languagePreferencesJson,validLanguagePreferences>;
struct LanguageOverrides {std::optional<std::string> language,formattingLocale;bool ascii=false;};
inline bool applyLanguageOverrides(const LanguagePreferences &saved,const LanguageOverrides &overrides,LanguagePreferences *out,std::string *error=nullptr){
    if(!out)return false;
    auto value=saved;if(overrides.language)value.language=*overrides.language;if(overrides.formattingLocale)value.formattingLocale=*overrides.formattingLocale;if(overrides.ascii)value.textOrder=TerminalTextOrder::Ascii;
    if(!validLanguagePreferences(value,error))return false;
    *out=std::move(value);return true;
}
// Ordinary all-domain/typed-span integration is not available yet, even if an independent ICU test target exists.
// Saved/CLI requests never block mining or pretend to activate the independent primitives.
inline const char *effectiveNativeMessageLanguage(){return "en";}
inline const char *effectiveNativeNumberFormat(){return "legacy per-field formatting";}
inline const char *nativeLanguageFallbackReason(){return "Full native translation/typed-layout integration unavailable; legacy English presentation remains active.";}
} // namespace om
