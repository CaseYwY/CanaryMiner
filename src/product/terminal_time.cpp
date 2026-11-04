#include "terminal_time.h"
#include "localization.h"
#include "presentation_locales.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <memory>
#include <vector>
#if defined(CANARY_TERMINAL_ICU)
#include <unicode/uchar.h>
#include <unicode/udat.h>
#include <unicode/udatpg.h>
#include <unicode/ustring.h>
#endif
namespace om {
namespace {
bool tag(const std::string &text){return !text.empty()&&text.size()<=35&&std::all_of(text.begin(),text.end(),[](unsigned char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-';});}
std::string offsetLabel(int32_t seconds){
    if(!seconds)return "UTC";
    const unsigned n=static_cast<unsigned>(seconds<0?-seconds:seconds);char buffer[24];
    std::snprintf(buffer,sizeof(buffer),"UTC%c%02u:%02u:%02u",seconds<0?'-':'+',n/3600,n/60%60,n%60);return buffer;
}
std::string asciiTime(const TerminalTimeRequest &request,const CivilStamp &civil){
    char date[24],time[24];const auto ms=civil.millisecondOfDay;
    std::snprintf(date,sizeof(date),"%04d-%02u-%02u",civil.date.year,civil.date.month,civil.date.day);
    std::snprintf(time,sizeof(time),"%02u:%02u:%02u",ms/3600000,ms/60000%60,ms/1000%60);
    std::string result=request.fields==PresentationTimeFields::Time?std::string{}:date;
    if(request.fields!=PresentationTimeFields::Date){if(!result.empty())result+=' ';result+=time;
        if(request.milliseconds){char fraction[8];std::snprintf(fraction,sizeof(fraction),".%03u",ms%1000);result+=fraction;}}
    return result;
}
#if defined(CANARY_TERMINAL_ICU)
bool formatNative(const TerminalTimeRequest &request,const char *locale,TerminalTime &out){
    std::string id=std::string(locale)+"@calendar=gregorian";if(request.digits==PresentationDigits::Latin)id+=";numbers=latn";
    std::string skeleton=request.fields==PresentationTimeFields::Time?std::string{}:request.dateStyle==PresentationDateStyle::Numeric?"yMd":"yMMMd";
    if(request.fields!=PresentationTimeFields::Date){skeleton+=request.hours==PresentationHourCycle::Hour24?"HHmmss":"hhmmss";if(request.milliseconds)skeleton+="SSS";}
    UErrorCode error=U_ZERO_ERROR;bool inherited=false;
    const auto note=[&](){if(error==U_USING_DEFAULT_WARNING||error==U_USING_FALLBACK_WARNING)inherited=true;};
    std::unique_ptr<UDateTimePatternGenerator,decltype(&udatpg_close)> generator(udatpg_open(id.c_str(),&error),udatpg_close);note();
    if(U_FAILURE(error)||!generator)return false;
    std::vector<UChar> spec(skeleton.begin(),skeleton.end());std::array<UChar,256> pattern{};
    const auto size=udatpg_getBestPatternWithOptions(generator.get(),spec.data(),static_cast<int32_t>(spec.size()),UDATPG_MATCH_ALL_FIELDS_LENGTH,pattern.data(),static_cast<int32_t>(pattern.size()),&error);note();
    if(U_FAILURE(error)||size<=0||static_cast<std::size_t>(size)>=pattern.size())return false;
    // UTC conversion, DST and source database version belong to civilAt, not ICU. Formatting an exact
    // already-resolved wall instant in explicit GMT prevents a second or divergent timezone conversion.
    static constexpr UChar gmt[]={'G','M','T'};
    std::unique_ptr<UDateFormat,decltype(&udat_close)> formatter(udat_open(UDAT_PATTERN,UDAT_PATTERN,id.c_str(),gmt,3,pattern.data(),size,&error),udat_close);note();
    if(U_FAILURE(error)||!formatter)return false;
    const auto *calendar=udat_getCalendar(formatter.get());const auto *type=calendar?ucal_getType(calendar,&error):nullptr;
    if(U_FAILURE(error)||!type||std::string(type)!="gregorian")return false;
    const auto day=civilSerialDay(out.civil.date);if(!day)return false;
    const int64_t wall=*day*86400000+out.civil.millisecondOfDay; // bounded below 2^48; every millisecond is exact in UDate
    std::array<UChar,512> buffer{};const auto length=udat_format(formatter.get(),static_cast<UDate>(wall),buffer.data(),static_cast<int32_t>(buffer.size()),nullptr,&error);note();
    if(U_FAILURE(error)||length<=0||static_cast<std::size_t>(length)>=buffer.size())return false;
    std::vector<UChar> clean;for(int32_t at=0;at<length;){UChar32 cp;U16_NEXT(buffer.data(),at,length,cp);if(u_hasBinaryProperty(cp,UCHAR_BIDI_CONTROL))continue;
        if(cp<=0xffff)clean.push_back(static_cast<UChar>(cp));else{clean.push_back(U16_LEAD(cp));clean.push_back(U16_TRAIL(cp));}}
    std::string text(2048,'\0');int32_t written=0;u_strToUTF8(text.data(),static_cast<int32_t>(text.size()),&written,clean.data(),static_cast<int32_t>(clean.size()),&error);
    if(U_FAILURE(error)||written<=0||written>=static_cast<int32_t>(text.size()))return false;
    text.resize(static_cast<std::size_t>(written));if(!i18n::safeMessageText(text))return false;
    out.text=std::move(text);out.resourceInheritance=inherited;return true;
}
#endif
}
bool formatTerminalTime(const TerminalTimeRequest &request,TerminalTime *out){
    if(!out||!request.utcMs||!tag(request.formattingLocale)||static_cast<unsigned>(request.digits)>1||static_cast<unsigned>(request.fields)>2||
        static_cast<unsigned>(request.dateStyle)>1||static_cast<unsigned>(request.hours)>1||static_cast<unsigned>(request.tone)>static_cast<unsigned>(Tone::Green))return false;
    TerminalTime result;result.civil=civilAt(request.clock,*request.utcMs);if(result.civil.status!=CivilStatus::Ok)return false;
    result.utcMs=*request.utcMs;result.clock=request.clock;result.zoneLabel=offsetLabel(result.civil.offsetSeconds);
    if(request.clock.kind==CivilClockKind::NamedIana)result.zoneLabel=request.clock.zone+" "+result.zoneLabel;
    const auto &locales=presentation_detail::formattingLocales();const auto *format=&locales[0];for(const auto &entry:locales)if(request.formattingLocale==entry.id){format=&entry;break;}
    result.formattingLocale=format->id;result.localeFallback=result.formattingLocale!=request.formattingLocale;result.ascii=asciiTime(request,result.civil);
    if(request.asciiOnly){result.text=result.ascii;result.asciiRecovery=true;}
#if defined(CANARY_TERMINAL_ICU)
    else if(!formatNative(request,format->icu,result))return false;
#else
    else{result.text=result.ascii;result.backendFallback=true;}
#endif
    const auto label=" ["+result.zoneLabel+"]";
    result.segment=TerminalSegment{TerminalTextSpan{result.text+label,result.ascii+label,false,TerminalTextAtom::LeftToRight},request.tone};
    // Ensure all native-generated human text can pass the same ordinary scalar/control/atomic boundary.
    TerminalTextPolicy policy;policy.order=request.asciiOnly?TerminalTextOrder::Ascii:TerminalTextOrder::Logical;policy.columns=4096;ColoredTerminalLine line;
    if(!layoutTerminalSegments({result.segment},policy,&line)||line.line.clipped)return false;
    *out=std::move(result);return true;
}
} // namespace om
