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
bool prepare(const TerminalSettingsActionMessageRequest &request,TerminalMessageRequest &message,terminal_message_detail::Templates &templates){
    if(!bounded(request.language,request.arguments))return false;
    for(const auto &argument:request.arguments){const auto &span=argument.value.content;
        if(argument.name=="state"&&(!span.invariant||(span.text!="x"&&span.text!=" ")))return false;
    }
    templates.requested=i18n::settingsActionMessage(request.language,request.key);templates.english=i18n::settingsActionMessage("en",request.key);
    if(templates.requested.text.empty()||templates.english.text.empty())return false;
    for(const auto &pack:i18n::settingsActionLocalePacks())if(pack.id==request.language)templates.requestedReview=pack.review;
    message.language=request.language;message.literalTone=request.literalTone;message.arguments=request.arguments;return true;
}
bool prepare(const TerminalSettingsDiagnosticMessageRequest &request,TerminalMessageRequest &message,terminal_message_detail::Templates &templates){
    if(!bounded(request.language,request.arguments))return false;
    templates.requested=i18n::settingsDiagnosticMessage(request.language,request.key);templates.english=i18n::settingsDiagnosticMessage("en",request.key);
    if(templates.requested.text.empty()||templates.english.text.empty())return false;
    for(const auto &pack:i18n::settingsDiagnosticLocalePacks())if(pack.id==request.language)templates.requestedReview=pack.review;
    message.language=request.language;message.literalTone=request.literalTone;message.arguments=request.arguments;return true;
}
bool prepare(const TerminalLanguageFieldMessageRequest &request,TerminalMessageRequest &message,terminal_message_detail::Templates &templates){
    if(!terminal_message_detail::validLanguageTag(request.language))return false;
    templates.requested=i18n::languageFieldMessage(request.language,request.key);templates.english=i18n::languageFieldMessage("en",request.key);
    if(templates.requested.text.empty()||templates.english.text.empty())return false;
    for(const auto &pack:i18n::languageFieldLocalePacks())if(pack.id==request.language)templates.requestedReview=pack.review;
    message.language=request.language;message.literalTone=request.literalTone;return true;
}
bool prepare(const TerminalLanguagePanelMessageRequest &request,TerminalMessageRequest &message,terminal_message_detail::Templates &templates){
    if(!bounded(request.language,request.arguments))return false;
    for(const auto &argument:request.arguments)if(argument.name=="value"&&!argument.value.content.invariant)return false;
    templates.requested=i18n::languagePanelMessage(request.language,request.key);templates.english=i18n::languagePanelMessage("en",request.key);
    if(templates.requested.text.empty()||templates.english.text.empty())return false;
    for(const auto &pack:i18n::languagePanelLocalePacks())if(pack.id==request.language)templates.requestedReview=pack.review;
    message.language=request.language;message.literalTone=request.literalTone;message.arguments=request.arguments;return true;
}
bool prepare(const TerminalRuntimeFieldMessageRequest &request,TerminalMessageRequest &message,terminal_message_detail::Templates &templates){
    if(!bounded(request.language,request.arguments))return false;
    for(const auto &argument:request.arguments){const bool quantity=argument.name=="wait"||(request.key==i18n::RuntimeFieldMessageKey::WaitChange&&(argument.name=="before"||argument.name=="after"));
        if(quantity&&argument.value.content.atom!=TerminalTextAtom::LeftToRight)return false;
    }
    templates.requested=i18n::runtimeFieldMessage(request.language,request.key);templates.english=i18n::runtimeFieldMessage("en",request.key);
    if(templates.requested.text.empty()||templates.english.text.empty())return false;
    for(const auto &pack:i18n::runtimeFieldLocalePacks())if(pack.id==request.language)templates.requestedReview=pack.review;
    message.language=request.language;message.literalTone=request.literalTone;message.arguments=request.arguments;return true;
}
bool prepare(const TerminalRuntimePanelMessageRequest &request,TerminalMessageRequest &message,terminal_message_detail::Templates &templates){
    if(!bounded(request.language,request.arguments))return false;
    for(const auto &argument:request.arguments)if(argument.name=="input"&&(!argument.value.content.invariant||argument.value.content.text.size()>16))return false;
    templates.requested=i18n::runtimePanelMessage(request.language,request.key);templates.english=i18n::runtimePanelMessage("en",request.key);
    if(templates.requested.text.empty()||templates.english.text.empty())return false;
    for(const auto &pack:i18n::runtimePanelLocalePacks())if(pack.id==request.language)templates.requestedReview=pack.review;
    message.language=request.language;message.literalTone=request.literalTone;message.arguments=request.arguments;return true;
}
bool prepare(const TerminalActivationMessageRequest &request,TerminalMessageRequest &message,terminal_message_detail::Templates &templates){
    if(!terminal_message_detail::validLanguageTag(request.language))return false;
    templates.requested=i18n::activationMessage(request.language,request.key);templates.english=i18n::activationMessage("en",request.key);
    if(templates.requested.text.empty()||templates.english.text.empty())return false;
    for(const auto &pack:i18n::activationLocalePacks())if(pack.id==request.language)templates.requestedReview=pack.review;
    message.language=request.language;message.literalTone=request.literalTone;return true;
}
bool prepare(const TerminalGameFieldMessageRequest &request,TerminalMessageRequest &message,terminal_message_detail::Templates &templates){
    if(!terminal_message_detail::validLanguageTag(request.language))return false;
    templates.requested=i18n::gameFieldMessage(request.language,request.key);templates.english=i18n::gameFieldMessage("en",request.key);
    if(templates.requested.text.empty()||templates.english.text.empty())return false;
    for(const auto &pack:i18n::gameFieldLocalePacks())if(pack.id==request.language)templates.requestedReview=pack.review;
    message.language=request.language;message.literalTone=request.literalTone;return true;
}
bool prepare(const TerminalGamePanelMessageRequest &request,TerminalMessageRequest &message,terminal_message_detail::Templates &templates){
    if(!terminal_message_detail::validLanguageTag(request.language))return false;
    templates.requested=i18n::gamePanelMessage(request.language,request.key);templates.english=i18n::gamePanelMessage("en",request.key);
    if(templates.requested.text.empty()||templates.english.text.empty())return false;
    for(const auto &pack:i18n::gamePanelLocalePacks())if(pack.id==request.language)templates.requestedReview=pack.review;
    message.language=request.language;message.literalTone=request.literalTone;return true;
}
bool prepare(const TerminalGameValueMessageRequest &request,TerminalMessageRequest &message,terminal_message_detail::Templates &templates){
    if(!bounded(request.language,request.arguments))return false;
    using Key=i18n::GameValueMessageKey;
    const bool scope=request.key==Key::ScopeValue||request.key==Key::ScopeChange;
    for(const auto &argument:request.arguments){
        const auto &content=argument.value.content;if(content.text.size()>128||content.ascii.size()>128)return false;
        const bool value=argument.name=="value"||argument.name=="before"||argument.name=="after";
        if((argument.name=="input"||argument.name=="mode"||(value&&!scope))&&!content.invariant)return false;
        if(value&&scope&&content.invariant)return false;
    }
    templates.requested=i18n::gameValueMessage(request.language,request.key);templates.english=i18n::gameValueMessage("en",request.key);
    if(templates.requested.text.empty()||templates.english.text.empty())return false;
    for(const auto &pack:i18n::gameValueLocalePacks())if(pack.id==request.language)templates.requestedReview=pack.review;
    message.language=request.language;message.literalTone=request.literalTone;message.arguments=request.arguments;return true;
}
bool prepare(const TerminalPoolFieldMessageRequest &request,TerminalMessageRequest &message,terminal_message_detail::Templates &templates){
    if(!terminal_message_detail::validLanguageTag(request.language))return false;
    templates.requested=i18n::poolFieldMessage(request.language,request.key);templates.english=i18n::poolFieldMessage("en",request.key);
    if(templates.requested.text.empty()||templates.english.text.empty())return false;
    for(const auto &pack:i18n::poolFieldLocalePacks())if(pack.id==request.language)templates.requestedReview=pack.review;
    message.language=request.language;message.literalTone=request.literalTone;return true;
}
bool prepare(const TerminalPoolNoticeMessageRequest &request,TerminalMessageRequest &message,terminal_message_detail::Templates &templates){
    if(!terminal_message_detail::validLanguageTag(request.language))return false;
    templates.requested=i18n::poolNoticeMessage(request.language,request.key);templates.english=i18n::poolNoticeMessage("en",request.key);
    if(templates.requested.text.empty()||templates.english.text.empty())return false;
    for(const auto &pack:i18n::poolNoticeLocalePacks())if(pack.id==request.language)templates.requestedReview=pack.review;
    message.language=request.language;message.literalTone=request.literalTone;return true;
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
bool layoutTerminalSettingsActionMessage(const TerminalSettingsActionMessageRequest &request,const TerminalTextPolicy &policy,TerminalMessageLine *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::layoutTemplate(message,templates,policy,out);
}
bool wrapTerminalSettingsActionMessage(const TerminalSettingsActionMessageRequest &request,const TerminalTextPolicy &policy,std::size_t maximumLines,TerminalMessageRows *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::wrapTemplate(message,templates,policy,maximumLines,out);
}
bool renderTerminalSettingsActionMessage(const TerminalSettingsActionMessageRequest &request,const TerminalTextPolicy &policy,ColorLevel color,PaintedTerminalMessageLine *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::renderTemplate(message,templates,policy,color,out);
}
bool renderWrappedTerminalSettingsActionMessage(const TerminalSettingsActionMessageRequest &request,const TerminalTextPolicy &policy,std::size_t maximumLines,ColorLevel color,PaintedTerminalMessageRows *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::renderWrappedTemplate(message,templates,policy,maximumLines,color,out);
}
bool layoutTerminalSettingsDiagnosticMessage(const TerminalSettingsDiagnosticMessageRequest &request,const TerminalTextPolicy &policy,TerminalMessageLine *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::layoutTemplate(message,templates,policy,out);
}
bool wrapTerminalSettingsDiagnosticMessage(const TerminalSettingsDiagnosticMessageRequest &request,const TerminalTextPolicy &policy,std::size_t maximumLines,TerminalMessageRows *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::wrapTemplate(message,templates,policy,maximumLines,out);
}
bool renderTerminalSettingsDiagnosticMessage(const TerminalSettingsDiagnosticMessageRequest &request,const TerminalTextPolicy &policy,ColorLevel color,PaintedTerminalMessageLine *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::renderTemplate(message,templates,policy,color,out);
}
bool renderWrappedTerminalSettingsDiagnosticMessage(const TerminalSettingsDiagnosticMessageRequest &request,const TerminalTextPolicy &policy,std::size_t maximumLines,ColorLevel color,PaintedTerminalMessageRows *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::renderWrappedTemplate(message,templates,policy,maximumLines,color,out);
}
bool layoutTerminalLanguageFieldMessage(const TerminalLanguageFieldMessageRequest &request,const TerminalTextPolicy &policy,TerminalMessageLine *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::layoutTemplate(message,templates,policy,out);
}
bool wrapTerminalLanguageFieldMessage(const TerminalLanguageFieldMessageRequest &request,const TerminalTextPolicy &policy,std::size_t maximumLines,TerminalMessageRows *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::wrapTemplate(message,templates,policy,maximumLines,out);
}
bool renderTerminalLanguageFieldMessage(const TerminalLanguageFieldMessageRequest &request,const TerminalTextPolicy &policy,ColorLevel color,PaintedTerminalMessageLine *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::renderTemplate(message,templates,policy,color,out);
}
bool renderWrappedTerminalLanguageFieldMessage(const TerminalLanguageFieldMessageRequest &request,const TerminalTextPolicy &policy,std::size_t maximumLines,ColorLevel color,PaintedTerminalMessageRows *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::renderWrappedTemplate(message,templates,policy,maximumLines,color,out);
}
bool layoutTerminalLanguagePanelMessage(const TerminalLanguagePanelMessageRequest &request,const TerminalTextPolicy &policy,TerminalMessageLine *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::layoutTemplate(message,templates,policy,out);
}
bool wrapTerminalLanguagePanelMessage(const TerminalLanguagePanelMessageRequest &request,const TerminalTextPolicy &policy,std::size_t maximumLines,TerminalMessageRows *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::wrapTemplate(message,templates,policy,maximumLines,out);
}
bool renderTerminalLanguagePanelMessage(const TerminalLanguagePanelMessageRequest &request,const TerminalTextPolicy &policy,ColorLevel color,PaintedTerminalMessageLine *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::renderTemplate(message,templates,policy,color,out);
}
bool renderWrappedTerminalLanguagePanelMessage(const TerminalLanguagePanelMessageRequest &request,const TerminalTextPolicy &policy,std::size_t maximumLines,ColorLevel color,PaintedTerminalMessageRows *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::renderWrappedTemplate(message,templates,policy,maximumLines,color,out);
}
bool layoutTerminalRuntimeFieldMessage(const TerminalRuntimeFieldMessageRequest &request,const TerminalTextPolicy &policy,TerminalMessageLine *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::layoutTemplate(message,templates,policy,out);
}
bool wrapTerminalRuntimeFieldMessage(const TerminalRuntimeFieldMessageRequest &request,const TerminalTextPolicy &policy,std::size_t maximumLines,TerminalMessageRows *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::wrapTemplate(message,templates,policy,maximumLines,out);
}
bool renderTerminalRuntimeFieldMessage(const TerminalRuntimeFieldMessageRequest &request,const TerminalTextPolicy &policy,ColorLevel color,PaintedTerminalMessageLine *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::renderTemplate(message,templates,policy,color,out);
}
bool renderWrappedTerminalRuntimeFieldMessage(const TerminalRuntimeFieldMessageRequest &request,const TerminalTextPolicy &policy,std::size_t maximumLines,ColorLevel color,PaintedTerminalMessageRows *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::renderWrappedTemplate(message,templates,policy,maximumLines,color,out);
}
bool layoutTerminalRuntimePanelMessage(const TerminalRuntimePanelMessageRequest &request,const TerminalTextPolicy &policy,TerminalMessageLine *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::layoutTemplate(message,templates,policy,out);
}
bool wrapTerminalRuntimePanelMessage(const TerminalRuntimePanelMessageRequest &request,const TerminalTextPolicy &policy,std::size_t maximumLines,TerminalMessageRows *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::wrapTemplate(message,templates,policy,maximumLines,out);
}
bool renderTerminalRuntimePanelMessage(const TerminalRuntimePanelMessageRequest &request,const TerminalTextPolicy &policy,ColorLevel color,PaintedTerminalMessageLine *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::renderTemplate(message,templates,policy,color,out);
}
bool renderWrappedTerminalRuntimePanelMessage(const TerminalRuntimePanelMessageRequest &request,const TerminalTextPolicy &policy,std::size_t maximumLines,ColorLevel color,PaintedTerminalMessageRows *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::renderWrappedTemplate(message,templates,policy,maximumLines,color,out);
}
bool layoutTerminalActivationMessage(const TerminalActivationMessageRequest &request,const TerminalTextPolicy &policy,TerminalMessageLine *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::layoutTemplate(message,templates,policy,out);
}
bool wrapTerminalActivationMessage(const TerminalActivationMessageRequest &request,const TerminalTextPolicy &policy,std::size_t maximumLines,TerminalMessageRows *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::wrapTemplate(message,templates,policy,maximumLines,out);
}
bool renderTerminalActivationMessage(const TerminalActivationMessageRequest &request,const TerminalTextPolicy &policy,ColorLevel color,PaintedTerminalMessageLine *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::renderTemplate(message,templates,policy,color,out);
}
bool renderWrappedTerminalActivationMessage(const TerminalActivationMessageRequest &request,const TerminalTextPolicy &policy,std::size_t maximumLines,ColorLevel color,PaintedTerminalMessageRows *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::renderWrappedTemplate(message,templates,policy,maximumLines,color,out);
}
bool layoutTerminalGameFieldMessage(const TerminalGameFieldMessageRequest &request,const TerminalTextPolicy &policy,TerminalMessageLine *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::layoutTemplate(message,templates,policy,out);
}
bool wrapTerminalGameFieldMessage(const TerminalGameFieldMessageRequest &request,const TerminalTextPolicy &policy,std::size_t maximumLines,TerminalMessageRows *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::wrapTemplate(message,templates,policy,maximumLines,out);
}
bool renderTerminalGameFieldMessage(const TerminalGameFieldMessageRequest &request,const TerminalTextPolicy &policy,ColorLevel color,PaintedTerminalMessageLine *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::renderTemplate(message,templates,policy,color,out);
}
bool renderWrappedTerminalGameFieldMessage(const TerminalGameFieldMessageRequest &request,const TerminalTextPolicy &policy,std::size_t maximumLines,ColorLevel color,PaintedTerminalMessageRows *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::renderWrappedTemplate(message,templates,policy,maximumLines,color,out);
}
bool layoutTerminalGamePanelMessage(const TerminalGamePanelMessageRequest &request,const TerminalTextPolicy &policy,TerminalMessageLine *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::layoutTemplate(message,templates,policy,out);
}
bool wrapTerminalGamePanelMessage(const TerminalGamePanelMessageRequest &request,const TerminalTextPolicy &policy,std::size_t maximumLines,TerminalMessageRows *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::wrapTemplate(message,templates,policy,maximumLines,out);
}
bool renderTerminalGamePanelMessage(const TerminalGamePanelMessageRequest &request,const TerminalTextPolicy &policy,ColorLevel color,PaintedTerminalMessageLine *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::renderTemplate(message,templates,policy,color,out);
}
bool renderWrappedTerminalGamePanelMessage(const TerminalGamePanelMessageRequest &request,const TerminalTextPolicy &policy,std::size_t maximumLines,ColorLevel color,PaintedTerminalMessageRows *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::renderWrappedTemplate(message,templates,policy,maximumLines,color,out);
}
bool layoutTerminalGameValueMessage(const TerminalGameValueMessageRequest &request,const TerminalTextPolicy &policy,TerminalMessageLine *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::layoutTemplate(message,templates,policy,out);
}
bool wrapTerminalGameValueMessage(const TerminalGameValueMessageRequest &request,const TerminalTextPolicy &policy,std::size_t maximumLines,TerminalMessageRows *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::wrapTemplate(message,templates,policy,maximumLines,out);
}
bool renderTerminalGameValueMessage(const TerminalGameValueMessageRequest &request,const TerminalTextPolicy &policy,ColorLevel color,PaintedTerminalMessageLine *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::renderTemplate(message,templates,policy,color,out);
}
bool renderWrappedTerminalGameValueMessage(const TerminalGameValueMessageRequest &request,const TerminalTextPolicy &policy,std::size_t maximumLines,ColorLevel color,PaintedTerminalMessageRows *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::renderWrappedTemplate(message,templates,policy,maximumLines,color,out);
}
bool layoutTerminalPoolFieldMessage(const TerminalPoolFieldMessageRequest &request,const TerminalTextPolicy &policy,TerminalMessageLine *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::layoutTemplate(message,templates,policy,out);
}
bool wrapTerminalPoolFieldMessage(const TerminalPoolFieldMessageRequest &request,const TerminalTextPolicy &policy,std::size_t maximumLines,TerminalMessageRows *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::wrapTemplate(message,templates,policy,maximumLines,out);
}
bool renderTerminalPoolFieldMessage(const TerminalPoolFieldMessageRequest &request,const TerminalTextPolicy &policy,ColorLevel color,PaintedTerminalMessageLine *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::renderTemplate(message,templates,policy,color,out);
}
bool renderWrappedTerminalPoolFieldMessage(const TerminalPoolFieldMessageRequest &request,const TerminalTextPolicy &policy,std::size_t maximumLines,ColorLevel color,PaintedTerminalMessageRows *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::renderWrappedTemplate(message,templates,policy,maximumLines,color,out);
}
bool layoutTerminalPoolNoticeMessage(const TerminalPoolNoticeMessageRequest &request,const TerminalTextPolicy &policy,TerminalMessageLine *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::layoutTemplate(message,templates,policy,out);
}
bool wrapTerminalPoolNoticeMessage(const TerminalPoolNoticeMessageRequest &request,const TerminalTextPolicy &policy,std::size_t maximumLines,TerminalMessageRows *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::wrapTemplate(message,templates,policy,maximumLines,out);
}
bool renderTerminalPoolNoticeMessage(const TerminalPoolNoticeMessageRequest &request,const TerminalTextPolicy &policy,ColorLevel color,PaintedTerminalMessageLine *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::renderTemplate(message,templates,policy,color,out);
}
bool renderWrappedTerminalPoolNoticeMessage(const TerminalPoolNoticeMessageRequest &request,const TerminalTextPolicy &policy,std::size_t maximumLines,ColorLevel color,PaintedTerminalMessageRows *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::renderWrappedTemplate(message,templates,policy,maximumLines,color,out);
}
} // namespace om
