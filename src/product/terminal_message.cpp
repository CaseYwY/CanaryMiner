#include "terminal_message.h"
#include "terminal_message_internal.h"
namespace om {
namespace {
bool commonTemplates(const TerminalMessageRequest &request,terminal_message_detail::Templates &templates){
    templates.requested=i18n::message(request.language,request.key);templates.english=i18n::message("en",request.key);
    for(const auto &pack:i18n::localePacks())if(pack.id==request.language)templates.requestedReview=pack.review;
    return !templates.requested.text.empty()&&!templates.english.text.empty();
}
bool compose(const TerminalMessageRequest &request,const terminal_message_detail::Templates &templates,const TerminalTextPolicy &policy,std::vector<TerminalSegment> &segments,TerminalMessageMetadata &metadata){
    if(!terminal_message_detail::validLanguageTag(request.language)||request.arguments.size()>8||static_cast<unsigned>(request.literalTone)>static_cast<unsigned>(Tone::Green))return false;
    const auto &requested=templates.requested;
    if(templates.english.locale!="en"||!i18n::validTemplateTranslation(templates.english.text,requested.text))return false;
    metadata.requestedLanguage=request.language;metadata.languageFallback=requested.fallback;metadata.requestedReview=templates.requestedReview;
    metadata.asciiRecovery=policy.order==TerminalTextOrder::Ascii;
    metadata.backendFallback=!metadata.asciiRecovery&&!terminalTextBackend().unicode;
    const bool recover=metadata.asciiRecovery||metadata.backendFallback;
    const auto selected=recover?templates.english:requested;
    metadata.renderedLanguage=std::string(selected.locale);metadata.renderedReview=selected.review;metadata.rtl=selected.rtl;
    std::vector<std::string_view> names;if(!i18n::templateParameters(selected.text,&names))return false;
    names.erase(std::unique(names.begin(),names.end()),names.end());
    std::vector<std::string_view> supplied;for(const auto &arg:request.arguments)supplied.push_back(arg.name);std::sort(supplied.begin(),supplied.end());
    if(names!=supplied||std::adjacent_find(supplied.begin(),supplied.end())!=supplied.end())return false;
    const auto literal=[&](std::string_view text){if(text.empty())return;
        const auto recovery=recover?TerminalTextRecovery::RequireAscii:TerminalTextRecovery::UnicodeOnly;
        segments.emplace_back(TerminalTextSpan{std::string(text),{},false,TerminalTextAtom::None,recovery},request.literalTone);metadata.sources.emplace_back();};
    std::size_t at=0;
    while(at<selected.text.size()){
        const auto start=selected.text.find('{',at);if(start==selected.text.npos){literal(selected.text.substr(at));break;}
        literal(selected.text.substr(at,start-at));const auto end=selected.text.find('}',start+1);const auto name=selected.text.substr(start+1,end-start-1);
        const auto argument=std::find_if(request.arguments.begin(),request.arguments.end(),[&](const auto &entry){return entry.name==name;});if(argument==request.arguments.end())return false;
        auto value=argument->value;
        if(recover){if(value.content.recovery==TerminalTextRecovery::UnicodeOnly)return false;
            // Validate the original typed argument before selecting its explicit recovery bytes. An injected
            // control, invalid invariant, malformed UTF-8 or bad enum cannot be hidden behind a safe fallback.
            TerminalTextPolicy check=policy;check.order=TerminalTextOrder::Ascii;check.columns=4096;ColoredTerminalLine validated;
            if(!layoutTerminalSegments({value},check,&validated))return false;
            if(!value.content.ascii.empty())value.content.text=value.content.ascii;
            value.content.ascii.clear();
        }
        segments.push_back(std::move(value));metadata.sources.push_back(argument->name);at=end+1;
    }
    return true;
}
}
namespace terminal_message_detail {
bool validLanguageTag(const std::string &text){return !text.empty()&&text.size()<=35&&std::all_of(text.begin(),text.end(),[](unsigned char ch){return (ch>='a'&&ch<='z')||(ch>='A'&&ch<='Z')||(ch>='0'&&ch<='9')||ch=='-';});}
bool layoutTemplate(const TerminalMessageRequest &request,const Templates &templates,const TerminalTextPolicy &policy,TerminalMessageLine *out){
    if(!out)return false;
    std::vector<TerminalSegment> segments;TerminalMessageLine result;if(!compose(request,templates,policy,segments,result.metadata)||!layoutTerminalSegments(segments,policy,&result.line))return false;
    *out=std::move(result);return true;
}
bool wrapTemplate(const TerminalMessageRequest &request,const Templates &templates,const TerminalTextPolicy &policy,std::size_t maximumLines,TerminalMessageRows *out){
    if(!out)return false;
    std::vector<TerminalSegment> segments;TerminalMessageRows result;if(!compose(request,templates,policy,segments,result.metadata)||!wrapTerminalSegments(segments,policy,maximumLines,&result.rows))return false;
    *out=std::move(result);return true;
}
bool renderTemplate(const TerminalMessageRequest &request,const Templates &templates,const TerminalTextPolicy &policy,ColorLevel color,PaintedTerminalMessageLine *out){
    if(!out)return false;
    std::vector<TerminalSegment> segments;PaintedTerminalMessageLine result;
    if(!compose(request,templates,policy,segments,result.metadata)||!renderTerminalSegments(segments,policy,color,&result.text))return false;
    *out=std::move(result);return true;
}
bool renderWrappedTemplate(const TerminalMessageRequest &request,const Templates &templates,const TerminalTextPolicy &policy,std::size_t maximumLines,ColorLevel color,PaintedTerminalMessageRows *out){
    if(!out)return false;
    std::vector<TerminalSegment> segments;PaintedTerminalMessageRows result;
    if(!compose(request,templates,policy,segments,result.metadata)||!renderWrappedTerminalSegments(segments,policy,maximumLines,color,&result.rows))return false;
    *out=std::move(result);return true;
}
} // namespace terminal_message_detail
bool layoutTerminalMessage(const TerminalMessageRequest &request,const TerminalTextPolicy &policy,TerminalMessageLine *out){
    terminal_message_detail::Templates templates;return commonTemplates(request,templates)&&terminal_message_detail::layoutTemplate(request,templates,policy,out);
}
bool wrapTerminalMessage(const TerminalMessageRequest &request,const TerminalTextPolicy &policy,std::size_t maximumLines,TerminalMessageRows *out){
    terminal_message_detail::Templates templates;return commonTemplates(request,templates)&&terminal_message_detail::wrapTemplate(request,templates,policy,maximumLines,out);
}
bool renderTerminalMessage(const TerminalMessageRequest &request,const TerminalTextPolicy &policy,ColorLevel color,PaintedTerminalMessageLine *out){
    terminal_message_detail::Templates templates;return commonTemplates(request,templates)&&terminal_message_detail::renderTemplate(request,templates,policy,color,out);
}
bool renderWrappedTerminalMessage(const TerminalMessageRequest &request,const TerminalTextPolicy &policy,std::size_t maximumLines,ColorLevel color,PaintedTerminalMessageRows *out){
    terminal_message_detail::Templates templates;return commonTemplates(request,templates)&&terminal_message_detail::renderWrappedTemplate(request,templates,policy,maximumLines,color,out);
}
} // namespace om
