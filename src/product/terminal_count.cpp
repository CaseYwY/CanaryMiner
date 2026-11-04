#include "terminal_count.h"
#include "terminal_message_internal.h"
namespace om {
namespace {
struct PreparedCount {TerminalMessageRequest message;terminal_message_detail::Templates templates;PresentationNumber number;bool neutral=false;};
bool prepare(const TerminalCountRequest &request,const TerminalTextPolicy &policy,PreparedCount &out){
    const auto key=static_cast<std::size_t>(request.key);
    if(key>=2||request.count.empty()||request.count.size()>96||!terminal_message_detail::validLanguageTag(request.language)||
        static_cast<unsigned>(request.countTone)>static_cast<unsigned>(Tone::Green)||static_cast<unsigned>(request.literalTone)>static_cast<unsigned>(Tone::Green)||
        !std::all_of(request.count.begin(),request.count.end(),[](unsigned char ch){return ch>='0'&&ch<='9';})||static_cast<unsigned>(policy.order)>2)return false;
    auto asciiPolicy=policy;asciiPolicy.order=TerminalTextOrder::Ascii;TerminalTextLine validation;if(!layoutTerminalText({},asciiPolicy,&validation))return false;
    const auto &packs=i18n::countLocalePacks();const auto *selected=&packs[0];bool known=false;
    for(const auto &pack:packs)if(pack.id==request.language){selected=&pack;known=true;break;}
    PresentationNumberRequest number;number.decimal=request.count;number.language=request.language;number.formattingLocale=request.formattingLocale;
    number.digits=request.digits;number.grouping=request.grouping;number.fractionDigits=0;number.asciiOnly=policy.order==TerminalTextOrder::Ascii;
    PreparedCount result;if(!formatPresentationNumber(number,&result.number))return false;
    result.message.language=request.language;result.message.literalTone=request.literalTone;
    result.message.arguments={{"count",{TerminalTextSpan{result.number.text,result.number.ascii,false,TerminalTextAtom::LeftToRight},request.countTone}}};
    if(known)result.templates.requestedReview=selected->review;
    result.neutral=!result.number.pluralAvailable;
    if(result.neutral){const auto text=i18n::neutralCountTemplate(request.key);result.templates.english={text,"en",i18n::ReviewStatus::Source,false,false};
        result.templates.requested=result.templates.english;result.templates.requested.fallback=!known;}
    else{const auto category=i18n::countCategoryIndex(result.number.pluralCategory);if(!category)return false;
        number.language="en";PresentationNumber english;if(!formatPresentationNumber(number,&english)||!english.pluralAvailable)return false;
        const auto sourceCategory=i18n::countCategoryIndex(english.pluralCategory);if(!sourceCategory)return false;
        result.templates.english={packs[0].messages[key][*sourceCategory],"en",packs[0].review,false,false};
        result.templates.requested={selected->messages[key][*category],selected->id,selected->review,!known,selected->rtl};
    }
    out=std::move(result);return true;
}
}
bool layoutTerminalCount(const TerminalCountRequest &request,const TerminalTextPolicy &policy,TerminalCountLine *out){
    if(!out)return false;
    PreparedCount prepared;if(!prepare(request,policy,prepared))return false;TerminalCountLine result;
    if(!terminal_message_detail::layoutTemplate(prepared.message,prepared.templates,policy,&result.message))return false;
    result.number=std::move(prepared.number);result.neutral=prepared.neutral;*out=std::move(result);return true;
}
bool wrapTerminalCount(const TerminalCountRequest &request,const TerminalTextPolicy &policy,std::size_t maximumLines,TerminalCountRows *out){
    if(!out||!maximumLines||maximumLines>4096)return false;
    PreparedCount prepared;if(!prepare(request,policy,prepared))return false;TerminalCountRows result;
    if(!terminal_message_detail::wrapTemplate(prepared.message,prepared.templates,policy,maximumLines,&result.message))return false;
    result.number=std::move(prepared.number);result.neutral=prepared.neutral;*out=std::move(result);return true;
}
bool renderTerminalCount(const TerminalCountRequest &request,const TerminalTextPolicy &policy,ColorLevel color,PaintedTerminalCountLine *out){
    if(!out||static_cast<unsigned>(color)>static_cast<unsigned>(ColorLevel::TrueColor))return false;
    PreparedCount prepared;if(!prepare(request,policy,prepared))return false;PaintedTerminalCountLine result;
    if(!terminal_message_detail::renderTemplate(prepared.message,prepared.templates,policy,color,&result.message))return false;
    result.number=std::move(prepared.number);result.neutral=prepared.neutral;*out=std::move(result);return true;
}
bool renderWrappedTerminalCount(const TerminalCountRequest &request,const TerminalTextPolicy &policy,std::size_t maximumLines,ColorLevel color,PaintedTerminalCountRows *out){
    if(!out||!maximumLines||maximumLines>4096||static_cast<unsigned>(color)>static_cast<unsigned>(ColorLevel::TrueColor))return false;
    PreparedCount prepared;if(!prepare(request,policy,prepared))return false;PaintedTerminalCountRows result;
    if(!terminal_message_detail::renderWrappedTemplate(prepared.message,prepared.templates,policy,maximumLines,color,&result.message))return false;
    result.number=std::move(prepared.number);result.neutral=prepared.neutral;*out=std::move(result);return true;
}
} // namespace om
