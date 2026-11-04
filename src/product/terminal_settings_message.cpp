#include "terminal_settings_message.h"
#include "terminal_message_internal.h"
namespace om {
namespace {
bool bounded(const std::string &language,const std::vector<TerminalMessageArgument> &arguments){
    if(!terminal_message_detail::validLanguageTag(language)||arguments.size()>8)return false;
    for(const auto &argument:arguments)if(argument.name.size()>32||argument.value.content.text.size()>4096||argument.value.content.ascii.size()>4096)return false;
    return true;
}
bool prepare(const TerminalSettingsMessageRequest &request,TerminalMessageRequest &message,terminal_message_detail::Templates &templates){
    if(!bounded(request.language,request.arguments))return false;
    templates.requested=i18n::settingsMessage(request.language,request.key);templates.english=i18n::settingsMessage("en",request.key);
    if(templates.requested.text.empty()||templates.english.text.empty())return false;
    for(const auto &pack:i18n::settingsLocalePacks())if(pack.id==request.language)templates.requestedReview=pack.review;
    message.language=request.language;message.literalTone=request.literalTone;message.arguments=request.arguments;return true;
}
bool prepare(const TerminalUiFieldMessageRequest &request,TerminalMessageRequest &message,terminal_message_detail::Templates &templates){
    if(!bounded(request.language,request.arguments))return false;
    for(const auto &argument:request.arguments){const auto &span=argument.value.content;
        if(argument.name=="state"&&(!span.invariant||(span.text!="x"&&span.text!=" ")))return false;
        if(argument.name=="value"&&(request.key==i18n::UiFieldMessageKey::Refresh||request.key==i18n::UiFieldMessageKey::Records)&&span.atom!=TerminalTextAtom::LeftToRight)return false;
        if(argument.name=="value"&&request.key==i18n::UiFieldMessageKey::TimeFormat&&(!span.invariant||(span.text!="ISO UTC"&&span.text!="UTC HH:MM:SS")))return false;
    }
    templates.requested=i18n::uiFieldMessage(request.language,request.key);templates.english=i18n::uiFieldMessage("en",request.key);
    if(templates.requested.text.empty()||templates.english.text.empty())return false;
    for(const auto &pack:i18n::uiFieldLocalePacks())if(pack.id==request.language)templates.requestedReview=pack.review;
    message.language=request.language;message.literalTone=request.literalTone;message.arguments=request.arguments;return true;
}
bool prepare(const TerminalSettingsChangeMessageRequest &request,TerminalMessageRequest &message,terminal_message_detail::Templates &templates){
    if(!bounded(request.language,request.arguments))return false;
    for(const auto &argument:request.arguments){const auto &span=argument.value.content;
        if(span.text.empty())return false;
        if((request.key==i18n::SettingsChangeMessageKey::Refresh||request.key==i18n::SettingsChangeMessageKey::LogRecords)&&span.atom!=TerminalTextAtom::LeftToRight)return false;
    }
    templates.requested=i18n::settingsChangeMessage(request.language,request.key);templates.english=i18n::settingsChangeMessage("en",request.key);
    if(templates.requested.text.empty()||templates.english.text.empty())return false;
    for(const auto &pack:i18n::settingsChangeLocalePacks())if(pack.id==request.language)templates.requestedReview=pack.review;
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
bool layoutTerminalUiFieldMessage(const TerminalUiFieldMessageRequest &request,const TerminalTextPolicy &policy,TerminalMessageLine *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::layoutTemplate(message,templates,policy,out);
}
bool wrapTerminalUiFieldMessage(const TerminalUiFieldMessageRequest &request,const TerminalTextPolicy &policy,std::size_t maximumLines,TerminalMessageRows *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::wrapTemplate(message,templates,policy,maximumLines,out);
}
bool renderTerminalUiFieldMessage(const TerminalUiFieldMessageRequest &request,const TerminalTextPolicy &policy,ColorLevel color,PaintedTerminalMessageLine *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::renderTemplate(message,templates,policy,color,out);
}
bool renderWrappedTerminalUiFieldMessage(const TerminalUiFieldMessageRequest &request,const TerminalTextPolicy &policy,std::size_t maximumLines,ColorLevel color,PaintedTerminalMessageRows *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::renderWrappedTemplate(message,templates,policy,maximumLines,color,out);
}
bool layoutTerminalSettingsChangeMessage(const TerminalSettingsChangeMessageRequest &request,const TerminalTextPolicy &policy,TerminalMessageLine *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::layoutTemplate(message,templates,policy,out);
}
bool wrapTerminalSettingsChangeMessage(const TerminalSettingsChangeMessageRequest &request,const TerminalTextPolicy &policy,std::size_t maximumLines,TerminalMessageRows *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::wrapTemplate(message,templates,policy,maximumLines,out);
}
bool renderTerminalSettingsChangeMessage(const TerminalSettingsChangeMessageRequest &request,const TerminalTextPolicy &policy,ColorLevel color,PaintedTerminalMessageLine *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::renderTemplate(message,templates,policy,color,out);
}
bool renderWrappedTerminalSettingsChangeMessage(const TerminalSettingsChangeMessageRequest &request,const TerminalTextPolicy &policy,std::size_t maximumLines,ColorLevel color,PaintedTerminalMessageRows *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::renderWrappedTemplate(message,templates,policy,maximumLines,color,out);
}
} // namespace om
