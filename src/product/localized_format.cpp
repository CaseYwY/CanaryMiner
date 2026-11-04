#include "localized_format.h"
#include "exact_decimal.h"
#include "localization.h"
#include "presentation_locales.h"
#include <algorithm>
#include <array>
#include <memory>
#include <vector>
#if defined(CANARY_TERMINAL_ICU)
#include <unicode/uchar.h>
#include <unicode/unumberformatter.h>
#include <unicode/upluralrules.h>
#include <unicode/ustring.h>
#endif
namespace om {
namespace {
using Locale=presentation_detail::FormattingLocale;
bool tag(const std::string &s){return !s.empty()&&s.size()<=35&&std::all_of(s.begin(),s.end(),[](unsigned char c){return (c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-';});}
const Locale &locale(const std::string &s){for(const auto &entry:presentation_detail::formattingLocales())if(s==entry.id)return entry;return presentation_detail::formattingLocales()[0];}
#if defined(CANARY_TERMINAL_ICU)
bool formatNative(const PresentationNumberRequest &request,PresentationNumber &out,const Locale &format,const Locale &language){
    std::string skeleton=request.fractionDigits?"."+std::string(request.fractionDigits,'0'):"precision-integer";
    skeleton+=request.grouping?" group-auto":" group-off";skeleton+=" rounding-mode-unnecessary";if(request.digits==PresentationDigits::Latin)skeleton+=" latin";
    std::vector<UChar> spec(skeleton.begin(),skeleton.end());UErrorCode error=U_ZERO_ERROR;
    std::unique_ptr<UNumberFormatter,decltype(&unumf_close)> formatter(unumf_openForSkeletonAndLocale(spec.data(),static_cast<int32_t>(spec.size()),format.icu,&error),unumf_close);
    std::unique_ptr<UFormattedNumber,decltype(&unumf_closeResult)> result(unumf_openResult(&error),unumf_closeResult);
    if(U_FAILURE(error)||!formatter||!result)return false;
    unumf_formatDecimal(formatter.get(),out.ascii.data(),static_cast<int32_t>(out.ascii.size()),result.get(),&error);if(U_FAILURE(error))return false;
    std::array<UChar,512> buffer{};const auto count=unumf_resultToString(result.get(),buffer.data(),static_cast<int32_t>(buffer.size()),&error);
    if(U_FAILURE(error)||count<0||static_cast<std::size_t>(count)>=buffer.size())return false;
    // ICU-created number-pattern marks are not accepted from the caller. Remove only those controlled
    // bidi marks; direction/isolation belongs to the subsequent native render layer, not numeric bytes.
    std::vector<UChar> clean;for(int32_t at=0;at<count;){UChar32 cp;U16_NEXT(buffer.data(),at,count,cp);if(u_hasBinaryProperty(cp,UCHAR_BIDI_CONTROL))continue;
        if(cp<=0xffff)clean.push_back(static_cast<UChar>(cp));else{clean.push_back(U16_LEAD(cp));clean.push_back(U16_TRAIL(cp));}}
    std::string text(2048,'\0');int32_t written=0;u_strToUTF8(text.data(),static_cast<int32_t>(text.size()),&written,clean.data(),static_cast<int32_t>(clean.size()),&error);
    if(U_FAILURE(error)||written<0||written>=static_cast<int32_t>(text.size()))return false;
    text.resize(static_cast<std::size_t>(written));
    if(!i18n::safeMessageText(text))return false;
    std::unique_ptr<UPluralRules,decltype(&uplrules_close)> rules(uplrules_openForType(language.icu,UPLURAL_TYPE_CARDINAL,&error),uplrules_close);
    std::array<UChar,16> category{};const auto length=rules?uplrules_selectFormatted(rules.get(),result.get(),category.data(),static_cast<int32_t>(category.size()),&error):-1;
    if(U_FAILURE(error)||length<=0||static_cast<std::size_t>(length)>=category.size())return false;
    std::string plural;for(int32_t i=0;i<length;++i){if(category[i]<'a'||category[i]>'z')return false;plural+=static_cast<char>(category[i]);}
    if(plural!="zero"&&plural!="one"&&plural!="two"&&plural!="few"&&plural!="many"&&plural!="other")return false;
    out.resourceInheritance=error==U_USING_DEFAULT_WARNING||error==U_USING_FALLBACK_WARNING;
    out.text=std::move(text);out.pluralCategory=std::move(plural);out.pluralAvailable=true;return true;
}
#endif
}
bool formatPresentationNumber(const PresentationNumberRequest &request,PresentationNumber *out){
    if(!out||request.fractionDigits>24||static_cast<unsigned>(request.digits)>1||!tag(request.formattingLocale)||!tag(request.language))return false;
    ExactDecimal value;if(!ExactDecimal::parse(request.decimal,&value))return false;
    const auto &locales=presentation_detail::formattingLocales();const auto &format=locale(request.formattingLocale);const Locale *language=&locales[0];for(std::size_t i=0;i<16;++i)if(request.language==locales[i].id){language=&locales[i];break;}
    PresentationNumber result;result.formattingLocale=format.id;result.language=language->id;result.localeFallback=result.formattingLocale!=request.formattingLocale;result.languageFallback=result.language!=request.language;
    if(!value.formatRatio(1,request.fractionDigits,&result.ascii))return false;
    ExactDecimal rounded;if(!ExactDecimal::parse(result.ascii,&rounded))return false;result.rounded=value.compare(rounded)!=0;
#if defined(CANARY_TERMINAL_ICU)
    if(!formatNative(request,result,format,*language))return false;
#else
    result.text=result.ascii;result.backendFallback=true; // no invented CLDR plural category when the maintained backend is disabled
#endif
    *out=std::move(result);return true;
}
const std::vector<std::string> &presentationFormattingLocales(){static const std::vector<std::string> ids=[](){std::vector<std::string> values;for(const auto &entry:presentation_detail::formattingLocales())values.emplace_back(entry.id);return values;}();return ids;}
} // namespace om
