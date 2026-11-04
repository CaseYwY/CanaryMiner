#include "terminal_settings_message.h"
#include "terminal_message_internal.h"
namespace om {
namespace {
bool prepare(const TerminalSettingsMessageRequest &request,TerminalMessageRequest &message,terminal_message_detail::Templates &templates){
    if(!terminal_message_detail::validLanguageTag(request.language)||request.arguments.size()>8)return false;
    for(const auto &argument:request.arguments)if(argument.name.size()>32||argument.value.content.text.size()>4096||argument.value.content.ascii.size()>4096)return false;
    templates.requested=i18n::settingsMessage(request.language,request.key);templates.english=i18n::settingsMessage("en",request.key);
    if(templates.requested.text.empty()||templates.english.text.empty())return false;
    for(const auto &pack:i18n::settingsLocalePacks())if(pack.id==request.language)templates.requestedReview=pack.review;
    message.language=request.language;message.literalTone=request.literalTone;message.arguments=request.arguments;return true;
}
}
bool layoutTerminalSettingsMessage(const TerminalSettingsMessageRequest &request,const TerminalTextPolicy &policy,TerminalMessageLine *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::layoutTemplate(message,templates,policy,out);
}
bool wrapTerminalSettingsMessage(const TerminalSettingsMessageRequest &request,const TerminalTextPolicy &policy,std::size_t maximumLines,TerminalMessageRows *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::wrapTemplate(message,templates,policy,maximumLines,out);
}
bool renderTerminalSettingsMessage(const TerminalSettingsMessageRequest &request,const TerminalTextPolicy &policy,ColorLevel color,PaintedTerminalMessageLine *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::renderTemplate(message,templates,policy,color,out);
}
bool renderWrappedTerminalSettingsMessage(const TerminalSettingsMessageRequest &request,const TerminalTextPolicy &policy,std::size_t maximumLines,ColorLevel color,PaintedTerminalMessageRows *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::renderWrappedTemplate(message,templates,policy,maximumLines,color,out);
}
} // namespace om
