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
bool prepare(const TerminalPoolPanelMessageRequest &request,TerminalMessageRequest &message,terminal_message_detail::Templates &templates){
    if(!terminal_message_detail::validLanguageTag(request.language))return false;
    templates.requested=i18n::poolPanelMessage(request.language,request.key);templates.english=i18n::poolPanelMessage("en",request.key);
    if(templates.requested.text.empty()||templates.english.text.empty())return false;
    for(const auto &pack:i18n::poolPanelLocalePacks())if(pack.id==request.language)templates.requestedReview=pack.review;
    message.language=request.language;message.literalTone=request.literalTone;return true;
}
bool prepare(const TerminalPoolValueMessageRequest &request,TerminalMessageRequest &message,terminal_message_detail::Templates &templates){
    if(!bounded(request.language,request.arguments))return false;
    const bool human=request.key==i18n::PoolValueMessageKey::HumanFieldValue;
    for(const auto &argument:request.arguments){
        const auto &content=argument.value.content;if(content.text.size()>300||content.ascii.size()>300)return false;
        if((argument.name=="input"||argument.name=="slot"||(argument.name=="value"&&!human))&&!content.invariant)return false;
        if(argument.name=="value"&&human&&content.invariant)return false;
    }
    templates.requested=i18n::poolValueMessage(request.language,request.key);templates.english=i18n::poolValueMessage("en",request.key);
    if(templates.requested.text.empty()||templates.english.text.empty())return false;
    for(const auto &pack:i18n::poolValueLocalePacks())if(pack.id==request.language)templates.requestedReview=pack.review;
    message.language=request.language;message.literalTone=request.literalTone;message.arguments=request.arguments;return true;
}
bool prepare(const TerminalPoolRuleMessageRequest &request,TerminalMessageRequest &message,terminal_message_detail::Templates &templates){
    if(!bounded(request.language,request.arguments))return false;
    for(const auto &argument:request.arguments){const auto &content=argument.value.content;
        if(!content.invariant||content.text.empty()||content.text.size()>300||content.ascii.size()>300)return false;
    }
    templates.requested=i18n::poolRuleMessage(request.language,request.key);templates.english=i18n::poolRuleMessage("en",request.key);
    if(templates.requested.text.empty()||templates.english.text.empty())return false;
    for(const auto &pack:i18n::poolRuleLocalePacks())if(pack.id==request.language)templates.requestedReview=pack.review;
    message.language=request.language;message.literalTone=request.literalTone;message.arguments=request.arguments;return true;
}
bool prepare(const TerminalManualMarketFieldMessageRequest &request,TerminalMessageRequest &message,terminal_message_detail::Templates &templates){
    if(!terminal_message_detail::validLanguageTag(request.language))return false;
    templates.requested=i18n::manualMarketFieldMessage(request.language,request.key);templates.english=i18n::manualMarketFieldMessage("en",request.key);
    if(templates.requested.text.empty()||templates.english.text.empty())return false;
    for(const auto &pack:i18n::manualMarketFieldLocalePacks())if(pack.id==request.language)templates.requestedReview=pack.review;
    message.language=request.language;message.literalTone=request.literalTone;return true;
}
bool prepare(const TerminalManualMarketNoticeMessageRequest &request,TerminalMessageRequest &message,terminal_message_detail::Templates &templates){
    if(!terminal_message_detail::validLanguageTag(request.language))return false;
    templates.requested=i18n::manualMarketNoticeMessage(request.language,request.key);templates.english=i18n::manualMarketNoticeMessage("en",request.key);
    if(templates.requested.text.empty()||templates.english.text.empty())return false;
    for(const auto &pack:i18n::manualMarketNoticeLocalePacks())if(pack.id==request.language)templates.requestedReview=pack.review;
    message.language=request.language;message.literalTone=request.literalTone;return true;
}
bool prepare(const TerminalManualMarketPanelMessageRequest &request,TerminalMessageRequest &message,terminal_message_detail::Templates &templates){
    if(!terminal_message_detail::validLanguageTag(request.language))return false;
    templates.requested=i18n::manualMarketPanelMessage(request.language,request.key);templates.english=i18n::manualMarketPanelMessage("en",request.key);
    if(templates.requested.text.empty()||templates.english.text.empty())return false;
    for(const auto &pack:i18n::manualMarketPanelLocalePacks())if(pack.id==request.language)templates.requestedReview=pack.review;
    message.language=request.language;message.literalTone=request.literalTone;return true;
}
bool prepare(const TerminalManualMarketValueMessageRequest &request,TerminalMessageRequest &message,terminal_message_detail::Templates &templates){
    if(!bounded(request.language,request.arguments))return false;
    using Key=i18n::ManualMarketValueMessageKey;const bool human=request.key==Key::HumanFieldValue||request.key==Key::HumanFieldChange;
    for(const auto &argument:request.arguments){const auto &content=argument.value.content;
        const bool humanValue=argument.name=="field"||(human&&(argument.name=="value"||argument.name=="before"||argument.name=="after"));
        const std::size_t maximum=humanValue?256:64;
        if(content.text.size()>maximum||content.ascii.size()>maximum||(argument.name!="input"&&content.text.empty()))return false;
        if(humanValue?content.invariant:!content.invariant)return false;
    }
    templates.requested=i18n::manualMarketValueMessage(request.language,request.key);templates.english=i18n::manualMarketValueMessage("en",request.key);
    if(templates.requested.text.empty()||templates.english.text.empty())return false;
    for(const auto &pack:i18n::manualMarketValueLocalePacks())if(pack.id==request.language)templates.requestedReview=pack.review;
    message.language=request.language;message.literalTone=request.literalTone;message.arguments=request.arguments;return true;
}
bool prepare(const TerminalManualMarketContextMessageRequest &request,TerminalMessageRequest &message,terminal_message_detail::Templates &templates){
    if(!bounded(request.language,request.arguments))return false;
    for(const auto &argument:request.arguments){const auto &content=argument.value.content;const bool state=argument.name=="state";
        const std::size_t maximum=state?256:64;
        if(content.text.empty()||content.text.size()>maximum||content.ascii.size()>maximum||(state?content.invariant:!content.invariant))return false;
    }
    templates.requested=i18n::manualMarketContextMessage(request.language,request.key);templates.english=i18n::manualMarketContextMessage("en",request.key);
    if(templates.requested.text.empty()||templates.english.text.empty())return false;
    for(const auto &pack:i18n::manualMarketContextLocalePacks())if(pack.id==request.language)templates.requestedReview=pack.review;
    message.language=request.language;message.literalTone=request.literalTone;message.arguments=request.arguments;return true;
}
bool prepare(const TerminalElectricityFieldMessageRequest &request,TerminalMessageRequest &message,terminal_message_detail::Templates &templates){
    if(!terminal_message_detail::validLanguageTag(request.language))return false;
    templates.requested=i18n::electricityFieldMessage(request.language,request.key);templates.english=i18n::electricityFieldMessage("en",request.key);
    if(templates.requested.text.empty()||templates.english.text.empty())return false;
    for(const auto &pack:i18n::electricityFieldLocalePacks())if(pack.id==request.language)templates.requestedReview=pack.review;
    message.language=request.language;message.literalTone=request.literalTone;return true;
}
bool prepare(const TerminalElectricityNoticeMessageRequest &request,TerminalMessageRequest &message,terminal_message_detail::Templates &templates){
    if(!terminal_message_detail::validLanguageTag(request.language))return false;
    templates.requested=i18n::electricityNoticeMessage(request.language,request.key);templates.english=i18n::electricityNoticeMessage("en",request.key);
    if(templates.requested.text.empty()||templates.english.text.empty())return false;
    for(const auto &pack:i18n::electricityNoticeLocalePacks())if(pack.id==request.language)templates.requestedReview=pack.review;
    message.language=request.language;message.literalTone=request.literalTone;return true;
}
bool prepare(const TerminalElectricityPanelMessageRequest &request,TerminalMessageRequest &message,terminal_message_detail::Templates &templates){
    if(!terminal_message_detail::validLanguageTag(request.language))return false;
    templates.requested=i18n::electricityPanelMessage(request.language,request.key);templates.english=i18n::electricityPanelMessage("en",request.key);
    if(templates.requested.text.empty()||templates.english.text.empty())return false;
    for(const auto &pack:i18n::electricityPanelLocalePacks())if(pack.id==request.language)templates.requestedReview=pack.review;
    message.language=request.language;message.literalTone=request.literalTone;return true;
}
bool prepare(const TerminalElectricityValueMessageRequest &request,TerminalMessageRequest &message,terminal_message_detail::Templates &templates){
    if(!bounded(request.language,request.arguments))return false;
    using Key=i18n::ElectricityValueMessageKey;const bool human=request.key==Key::HumanFieldValue||request.key==Key::HumanFieldChange;
    for(const auto &argument:request.arguments){const auto &content=argument.value.content;
        const bool humanValue=argument.name=="field"||(human&&(argument.name=="value"||argument.name=="before"||argument.name=="after"));
        const std::size_t maximum=humanValue?256:64;
        if(content.text.size()>maximum||content.ascii.size()>maximum||(argument.name!="input"&&content.text.empty())||(humanValue?content.invariant:!content.invariant))return false;
    }
    templates.requested=i18n::electricityValueMessage(request.language,request.key);templates.english=i18n::electricityValueMessage("en",request.key);
    if(templates.requested.text.empty()||templates.english.text.empty())return false;
    for(const auto &pack:i18n::electricityValueLocalePacks())if(pack.id==request.language)templates.requestedReview=pack.review;
    message.language=request.language;message.literalTone=request.literalTone;message.arguments=request.arguments;return true;
}
bool prepare(const TerminalElectricityContextMessageRequest &request,TerminalMessageRequest &message,terminal_message_detail::Templates &templates){
    if(!bounded(request.language,request.arguments))return false;
    for(const auto &argument:request.arguments){const auto &content=argument.value.content;const bool human=argument.name=="clock"||argument.name=="position";
        const std::size_t maximum=human?256:64;
        if(content.text.empty()||content.text.size()>maximum||content.ascii.size()>maximum||(human?content.invariant:!content.invariant))return false;
        if(argument.name=="position"&&content.atom!=TerminalTextAtom::LeftToRight)return false;
    }
    templates.requested=i18n::electricityContextMessage(request.language,request.key);templates.english=i18n::electricityContextMessage("en",request.key);
    if(templates.requested.text.empty()||templates.english.text.empty())return false;
    for(const auto &pack:i18n::electricityContextLocalePacks())if(pack.id==request.language)templates.requestedReview=pack.review;
    message.language=request.language;message.literalTone=request.literalTone;message.arguments=request.arguments;return true;
}
bool prepare(const TerminalElectricityTariffMessageRequest &request,TerminalMessageRequest &message,terminal_message_detail::Templates &templates){
    if(!bounded(request.language,request.arguments))return false;
    for(const auto &argument:request.arguments){const auto &content=argument.value.content;
        if(!content.invariant||content.text.empty()||content.text.size()>64||content.ascii.size()>64)return false;
    }
    templates.requested=i18n::electricityTariffMessage(request.language,request.key);templates.english=i18n::electricityTariffMessage("en",request.key);
    if(templates.requested.text.empty()||templates.english.text.empty())return false;
    for(const auto &pack:i18n::electricityTariffLocalePacks())if(pack.id==request.language)templates.requestedReview=pack.review;
    message.language=request.language;message.literalTone=request.literalTone;message.arguments=request.arguments;return true;
}
bool prepare(const TerminalEnergyAuthorizationFieldMessageRequest &request,TerminalMessageRequest &message,terminal_message_detail::Templates &templates){
    if(!terminal_message_detail::validLanguageTag(request.language))return false;
    templates.requested=i18n::energyAuthorizationFieldMessage(request.language,request.key);templates.english=i18n::energyAuthorizationFieldMessage("en",request.key);
    if(templates.requested.text.empty()||templates.english.text.empty())return false;
    for(const auto &pack:i18n::energyAuthorizationFieldLocalePacks())if(pack.id==request.language)templates.requestedReview=pack.review;
    message.language=request.language;message.literalTone=request.literalTone;return true;
}
bool prepare(const TerminalEnergyAuthorizationNoticeMessageRequest &request,TerminalMessageRequest &message,terminal_message_detail::Templates &templates){
    if(!terminal_message_detail::validLanguageTag(request.language))return false;
    templates.requested=i18n::energyAuthorizationNoticeMessage(request.language,request.key);templates.english=i18n::energyAuthorizationNoticeMessage("en",request.key);
    if(templates.requested.text.empty()||templates.english.text.empty())return false;
    for(const auto &pack:i18n::energyAuthorizationNoticeLocalePacks())if(pack.id==request.language)templates.requestedReview=pack.review;
    message.language=request.language;message.literalTone=request.literalTone;return true;
}
bool prepare(const TerminalEnergyAuthorizationPanelMessageRequest &request,TerminalMessageRequest &message,terminal_message_detail::Templates &templates){
    if(!terminal_message_detail::validLanguageTag(request.language))return false;
    templates.requested=i18n::energyAuthorizationPanelMessage(request.language,request.key);templates.english=i18n::energyAuthorizationPanelMessage("en",request.key);
    if(templates.requested.text.empty()||templates.english.text.empty())return false;
    for(const auto &pack:i18n::energyAuthorizationPanelLocalePacks())if(pack.id==request.language)templates.requestedReview=pack.review;
    message.language=request.language;message.literalTone=request.literalTone;return true;
}
bool prepare(const TerminalEnergyAuthorizationValueMessageRequest &request,TerminalMessageRequest &message,terminal_message_detail::Templates &templates){
    if(!bounded(request.language,request.arguments))return false;
    for(const auto &argument:request.arguments){const auto &content=argument.value.content;const bool human=argument.name=="field"||argument.name=="state"||(request.key==i18n::EnergyAuthorizationValueMessageKey::FieldHuman&&argument.name=="value");
        const std::size_t maximum=human?256:64;
        if(content.text.size()>maximum||content.ascii.size()>maximum||(argument.name!="input"&&content.text.empty())||(human?content.invariant:!content.invariant))return false;
    }
    templates.requested=i18n::energyAuthorizationValueMessage(request.language,request.key);templates.english=i18n::energyAuthorizationValueMessage("en",request.key);
    if(templates.requested.text.empty()||templates.english.text.empty())return false;
    for(const auto &pack:i18n::energyAuthorizationValueLocalePacks())if(pack.id==request.language)templates.requestedReview=pack.review;
    message.language=request.language;message.literalTone=request.literalTone;message.arguments=request.arguments;return true;
}
bool prepare(const TerminalEnergyAuthorizationPrivateMessageRequest &request,TerminalMessageRequest &message,terminal_message_detail::Templates &templates){
    if(!bounded(request.language,request.arguments))return false;
    for(const auto &argument:request.arguments){const auto &content=argument.value.content;const bool position=argument.name=="position";
        const std::size_t maximum=position?256:argument.name=="identity"?128:64;
        if(content.text.empty()||content.text.size()>maximum||content.ascii.size()>maximum||(position?content.invariant:!content.invariant))return false;
        if(position&&content.atom!=TerminalTextAtom::LeftToRight)return false;
    }
    templates.requested=i18n::energyAuthorizationPrivateMessage(request.language,request.key);templates.english=i18n::energyAuthorizationPrivateMessage("en",request.key);
    if(templates.requested.text.empty()||templates.english.text.empty())return false;
    for(const auto &pack:i18n::energyAuthorizationPrivateLocalePacks())if(pack.id==request.language)templates.requestedReview=pack.review;
    message.language=request.language;message.literalTone=request.literalTone;message.arguments=request.arguments;return true;
}
bool prepare(const TerminalTariffNoticeMessageRequest &request,TerminalMessageRequest &message,terminal_message_detail::Templates &templates){
    if(!terminal_message_detail::validLanguageTag(request.language))return false;
    templates.requested=i18n::tariffNoticeMessage(request.language,request.key);templates.english=i18n::tariffNoticeMessage("en",request.key);
    if(templates.requested.text.empty()||templates.english.text.empty())return false;
    for(const auto &pack:i18n::tariffNoticeLocalePacks())if(pack.id==request.language)templates.requestedReview=pack.review;
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
bool layoutTerminalPoolPanelMessage(const TerminalPoolPanelMessageRequest &request,const TerminalTextPolicy &policy,TerminalMessageLine *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::layoutTemplate(message,templates,policy,out);
}
bool wrapTerminalPoolPanelMessage(const TerminalPoolPanelMessageRequest &request,const TerminalTextPolicy &policy,std::size_t maximumLines,TerminalMessageRows *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::wrapTemplate(message,templates,policy,maximumLines,out);
}
bool renderTerminalPoolPanelMessage(const TerminalPoolPanelMessageRequest &request,const TerminalTextPolicy &policy,ColorLevel color,PaintedTerminalMessageLine *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::renderTemplate(message,templates,policy,color,out);
}
bool renderWrappedTerminalPoolPanelMessage(const TerminalPoolPanelMessageRequest &request,const TerminalTextPolicy &policy,std::size_t maximumLines,ColorLevel color,PaintedTerminalMessageRows *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::renderWrappedTemplate(message,templates,policy,maximumLines,color,out);
}
bool layoutTerminalPoolValueMessage(const TerminalPoolValueMessageRequest &request,const TerminalTextPolicy &policy,TerminalMessageLine *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::layoutTemplate(message,templates,policy,out);
}
bool wrapTerminalPoolValueMessage(const TerminalPoolValueMessageRequest &request,const TerminalTextPolicy &policy,std::size_t maximumLines,TerminalMessageRows *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::wrapTemplate(message,templates,policy,maximumLines,out);
}
bool renderTerminalPoolValueMessage(const TerminalPoolValueMessageRequest &request,const TerminalTextPolicy &policy,ColorLevel color,PaintedTerminalMessageLine *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::renderTemplate(message,templates,policy,color,out);
}
bool renderWrappedTerminalPoolValueMessage(const TerminalPoolValueMessageRequest &request,const TerminalTextPolicy &policy,std::size_t maximumLines,ColorLevel color,PaintedTerminalMessageRows *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::renderWrappedTemplate(message,templates,policy,maximumLines,color,out);
}
bool layoutTerminalPoolRuleMessage(const TerminalPoolRuleMessageRequest &request,const TerminalTextPolicy &policy,TerminalMessageLine *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::layoutTemplate(message,templates,policy,out);
}
bool wrapTerminalPoolRuleMessage(const TerminalPoolRuleMessageRequest &request,const TerminalTextPolicy &policy,std::size_t maximumLines,TerminalMessageRows *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::wrapTemplate(message,templates,policy,maximumLines,out);
}
bool renderTerminalPoolRuleMessage(const TerminalPoolRuleMessageRequest &request,const TerminalTextPolicy &policy,ColorLevel color,PaintedTerminalMessageLine *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::renderTemplate(message,templates,policy,color,out);
}
bool renderWrappedTerminalPoolRuleMessage(const TerminalPoolRuleMessageRequest &request,const TerminalTextPolicy &policy,std::size_t maximumLines,ColorLevel color,PaintedTerminalMessageRows *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::renderWrappedTemplate(message,templates,policy,maximumLines,color,out);
}
bool layoutTerminalManualMarketFieldMessage(const TerminalManualMarketFieldMessageRequest &request,const TerminalTextPolicy &policy,TerminalMessageLine *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::layoutTemplate(message,templates,policy,out);
}
bool wrapTerminalManualMarketFieldMessage(const TerminalManualMarketFieldMessageRequest &request,const TerminalTextPolicy &policy,std::size_t maximumLines,TerminalMessageRows *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::wrapTemplate(message,templates,policy,maximumLines,out);
}
bool renderTerminalManualMarketFieldMessage(const TerminalManualMarketFieldMessageRequest &request,const TerminalTextPolicy &policy,ColorLevel color,PaintedTerminalMessageLine *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::renderTemplate(message,templates,policy,color,out);
}
bool renderWrappedTerminalManualMarketFieldMessage(const TerminalManualMarketFieldMessageRequest &request,const TerminalTextPolicy &policy,std::size_t maximumLines,ColorLevel color,PaintedTerminalMessageRows *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::renderWrappedTemplate(message,templates,policy,maximumLines,color,out);
}
bool layoutTerminalManualMarketNoticeMessage(const TerminalManualMarketNoticeMessageRequest &request,const TerminalTextPolicy &policy,TerminalMessageLine *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::layoutTemplate(message,templates,policy,out);
}
bool wrapTerminalManualMarketNoticeMessage(const TerminalManualMarketNoticeMessageRequest &request,const TerminalTextPolicy &policy,std::size_t maximumLines,TerminalMessageRows *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::wrapTemplate(message,templates,policy,maximumLines,out);
}
bool renderTerminalManualMarketNoticeMessage(const TerminalManualMarketNoticeMessageRequest &request,const TerminalTextPolicy &policy,ColorLevel color,PaintedTerminalMessageLine *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::renderTemplate(message,templates,policy,color,out);
}
bool renderWrappedTerminalManualMarketNoticeMessage(const TerminalManualMarketNoticeMessageRequest &request,const TerminalTextPolicy &policy,std::size_t maximumLines,ColorLevel color,PaintedTerminalMessageRows *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::renderWrappedTemplate(message,templates,policy,maximumLines,color,out);
}
bool layoutTerminalManualMarketPanelMessage(const TerminalManualMarketPanelMessageRequest &request,const TerminalTextPolicy &policy,TerminalMessageLine *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::layoutTemplate(message,templates,policy,out);
}
bool wrapTerminalManualMarketPanelMessage(const TerminalManualMarketPanelMessageRequest &request,const TerminalTextPolicy &policy,std::size_t maximumLines,TerminalMessageRows *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::wrapTemplate(message,templates,policy,maximumLines,out);
}
bool renderTerminalManualMarketPanelMessage(const TerminalManualMarketPanelMessageRequest &request,const TerminalTextPolicy &policy,ColorLevel color,PaintedTerminalMessageLine *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::renderTemplate(message,templates,policy,color,out);
}
bool renderWrappedTerminalManualMarketPanelMessage(const TerminalManualMarketPanelMessageRequest &request,const TerminalTextPolicy &policy,std::size_t maximumLines,ColorLevel color,PaintedTerminalMessageRows *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::renderWrappedTemplate(message,templates,policy,maximumLines,color,out);
}
bool layoutTerminalManualMarketValueMessage(const TerminalManualMarketValueMessageRequest &request,const TerminalTextPolicy &policy,TerminalMessageLine *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::layoutTemplate(message,templates,policy,out);
}
bool wrapTerminalManualMarketValueMessage(const TerminalManualMarketValueMessageRequest &request,const TerminalTextPolicy &policy,std::size_t maximumLines,TerminalMessageRows *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::wrapTemplate(message,templates,policy,maximumLines,out);
}
bool renderTerminalManualMarketValueMessage(const TerminalManualMarketValueMessageRequest &request,const TerminalTextPolicy &policy,ColorLevel color,PaintedTerminalMessageLine *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::renderTemplate(message,templates,policy,color,out);
}
bool renderWrappedTerminalManualMarketValueMessage(const TerminalManualMarketValueMessageRequest &request,const TerminalTextPolicy &policy,std::size_t maximumLines,ColorLevel color,PaintedTerminalMessageRows *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::renderWrappedTemplate(message,templates,policy,maximumLines,color,out);
}
bool layoutTerminalManualMarketContextMessage(const TerminalManualMarketContextMessageRequest &request,const TerminalTextPolicy &policy,TerminalMessageLine *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::layoutTemplate(message,templates,policy,out);
}
bool wrapTerminalManualMarketContextMessage(const TerminalManualMarketContextMessageRequest &request,const TerminalTextPolicy &policy,std::size_t maximumLines,TerminalMessageRows *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::wrapTemplate(message,templates,policy,maximumLines,out);
}
bool renderTerminalManualMarketContextMessage(const TerminalManualMarketContextMessageRequest &request,const TerminalTextPolicy &policy,ColorLevel color,PaintedTerminalMessageLine *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::renderTemplate(message,templates,policy,color,out);
}
bool renderWrappedTerminalManualMarketContextMessage(const TerminalManualMarketContextMessageRequest &request,const TerminalTextPolicy &policy,std::size_t maximumLines,ColorLevel color,PaintedTerminalMessageRows *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::renderWrappedTemplate(message,templates,policy,maximumLines,color,out);
}
bool layoutTerminalElectricityFieldMessage(const TerminalElectricityFieldMessageRequest &request,const TerminalTextPolicy &policy,TerminalMessageLine *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::layoutTemplate(message,templates,policy,out);
}
bool wrapTerminalElectricityFieldMessage(const TerminalElectricityFieldMessageRequest &request,const TerminalTextPolicy &policy,std::size_t maximumLines,TerminalMessageRows *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::wrapTemplate(message,templates,policy,maximumLines,out);
}
bool renderTerminalElectricityFieldMessage(const TerminalElectricityFieldMessageRequest &request,const TerminalTextPolicy &policy,ColorLevel color,PaintedTerminalMessageLine *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::renderTemplate(message,templates,policy,color,out);
}
bool renderWrappedTerminalElectricityFieldMessage(const TerminalElectricityFieldMessageRequest &request,const TerminalTextPolicy &policy,std::size_t maximumLines,ColorLevel color,PaintedTerminalMessageRows *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::renderWrappedTemplate(message,templates,policy,maximumLines,color,out);
}
bool layoutTerminalElectricityNoticeMessage(const TerminalElectricityNoticeMessageRequest &request,const TerminalTextPolicy &policy,TerminalMessageLine *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::layoutTemplate(message,templates,policy,out);
}
bool wrapTerminalElectricityNoticeMessage(const TerminalElectricityNoticeMessageRequest &request,const TerminalTextPolicy &policy,std::size_t maximumLines,TerminalMessageRows *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::wrapTemplate(message,templates,policy,maximumLines,out);
}
bool renderTerminalElectricityNoticeMessage(const TerminalElectricityNoticeMessageRequest &request,const TerminalTextPolicy &policy,ColorLevel color,PaintedTerminalMessageLine *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::renderTemplate(message,templates,policy,color,out);
}
bool renderWrappedTerminalElectricityNoticeMessage(const TerminalElectricityNoticeMessageRequest &request,const TerminalTextPolicy &policy,std::size_t maximumLines,ColorLevel color,PaintedTerminalMessageRows *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::renderWrappedTemplate(message,templates,policy,maximumLines,color,out);
}
bool layoutTerminalElectricityPanelMessage(const TerminalElectricityPanelMessageRequest &request,const TerminalTextPolicy &policy,TerminalMessageLine *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::layoutTemplate(message,templates,policy,out);
}
bool wrapTerminalElectricityPanelMessage(const TerminalElectricityPanelMessageRequest &request,const TerminalTextPolicy &policy,std::size_t maximumLines,TerminalMessageRows *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::wrapTemplate(message,templates,policy,maximumLines,out);
}
bool renderTerminalElectricityPanelMessage(const TerminalElectricityPanelMessageRequest &request,const TerminalTextPolicy &policy,ColorLevel color,PaintedTerminalMessageLine *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::renderTemplate(message,templates,policy,color,out);
}
bool renderWrappedTerminalElectricityPanelMessage(const TerminalElectricityPanelMessageRequest &request,const TerminalTextPolicy &policy,std::size_t maximumLines,ColorLevel color,PaintedTerminalMessageRows *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::renderWrappedTemplate(message,templates,policy,maximumLines,color,out);
}
bool layoutTerminalElectricityValueMessage(const TerminalElectricityValueMessageRequest &request,const TerminalTextPolicy &policy,TerminalMessageLine *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::layoutTemplate(message,templates,policy,out);
}
bool wrapTerminalElectricityValueMessage(const TerminalElectricityValueMessageRequest &request,const TerminalTextPolicy &policy,std::size_t maximumLines,TerminalMessageRows *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::wrapTemplate(message,templates,policy,maximumLines,out);
}
bool renderTerminalElectricityValueMessage(const TerminalElectricityValueMessageRequest &request,const TerminalTextPolicy &policy,ColorLevel color,PaintedTerminalMessageLine *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::renderTemplate(message,templates,policy,color,out);
}
bool renderWrappedTerminalElectricityValueMessage(const TerminalElectricityValueMessageRequest &request,const TerminalTextPolicy &policy,std::size_t maximumLines,ColorLevel color,PaintedTerminalMessageRows *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::renderWrappedTemplate(message,templates,policy,maximumLines,color,out);
}
bool layoutTerminalElectricityContextMessage(const TerminalElectricityContextMessageRequest &request,const TerminalTextPolicy &policy,TerminalMessageLine *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::layoutTemplate(message,templates,policy,out);
}
bool wrapTerminalElectricityContextMessage(const TerminalElectricityContextMessageRequest &request,const TerminalTextPolicy &policy,std::size_t maximumLines,TerminalMessageRows *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::wrapTemplate(message,templates,policy,maximumLines,out);
}
bool renderTerminalElectricityContextMessage(const TerminalElectricityContextMessageRequest &request,const TerminalTextPolicy &policy,ColorLevel color,PaintedTerminalMessageLine *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::renderTemplate(message,templates,policy,color,out);
}
bool renderWrappedTerminalElectricityContextMessage(const TerminalElectricityContextMessageRequest &request,const TerminalTextPolicy &policy,std::size_t maximumLines,ColorLevel color,PaintedTerminalMessageRows *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::renderWrappedTemplate(message,templates,policy,maximumLines,color,out);
}
bool layoutTerminalElectricityTariffMessage(const TerminalElectricityTariffMessageRequest &request,const TerminalTextPolicy &policy,TerminalMessageLine *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::layoutTemplate(message,templates,policy,out);
}
bool wrapTerminalElectricityTariffMessage(const TerminalElectricityTariffMessageRequest &request,const TerminalTextPolicy &policy,std::size_t maximumLines,TerminalMessageRows *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::wrapTemplate(message,templates,policy,maximumLines,out);
}
bool renderTerminalElectricityTariffMessage(const TerminalElectricityTariffMessageRequest &request,const TerminalTextPolicy &policy,ColorLevel color,PaintedTerminalMessageLine *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::renderTemplate(message,templates,policy,color,out);
}
bool renderWrappedTerminalElectricityTariffMessage(const TerminalElectricityTariffMessageRequest &request,const TerminalTextPolicy &policy,std::size_t maximumLines,ColorLevel color,PaintedTerminalMessageRows *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::renderWrappedTemplate(message,templates,policy,maximumLines,color,out);
}
bool layoutTerminalEnergyAuthorizationFieldMessage(const TerminalEnergyAuthorizationFieldMessageRequest &request,const TerminalTextPolicy &policy,TerminalMessageLine *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::layoutTemplate(message,templates,policy,out);
}
bool wrapTerminalEnergyAuthorizationFieldMessage(const TerminalEnergyAuthorizationFieldMessageRequest &request,const TerminalTextPolicy &policy,std::size_t maximumLines,TerminalMessageRows *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::wrapTemplate(message,templates,policy,maximumLines,out);
}
bool renderTerminalEnergyAuthorizationFieldMessage(const TerminalEnergyAuthorizationFieldMessageRequest &request,const TerminalTextPolicy &policy,ColorLevel color,PaintedTerminalMessageLine *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::renderTemplate(message,templates,policy,color,out);
}
bool renderWrappedTerminalEnergyAuthorizationFieldMessage(const TerminalEnergyAuthorizationFieldMessageRequest &request,const TerminalTextPolicy &policy,std::size_t maximumLines,ColorLevel color,PaintedTerminalMessageRows *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::renderWrappedTemplate(message,templates,policy,maximumLines,color,out);
}
bool layoutTerminalEnergyAuthorizationNoticeMessage(const TerminalEnergyAuthorizationNoticeMessageRequest &request,const TerminalTextPolicy &policy,TerminalMessageLine *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::layoutTemplate(message,templates,policy,out);
}
bool wrapTerminalEnergyAuthorizationNoticeMessage(const TerminalEnergyAuthorizationNoticeMessageRequest &request,const TerminalTextPolicy &policy,std::size_t maximumLines,TerminalMessageRows *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::wrapTemplate(message,templates,policy,maximumLines,out);
}
bool renderTerminalEnergyAuthorizationNoticeMessage(const TerminalEnergyAuthorizationNoticeMessageRequest &request,const TerminalTextPolicy &policy,ColorLevel color,PaintedTerminalMessageLine *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::renderTemplate(message,templates,policy,color,out);
}
bool renderWrappedTerminalEnergyAuthorizationNoticeMessage(const TerminalEnergyAuthorizationNoticeMessageRequest &request,const TerminalTextPolicy &policy,std::size_t maximumLines,ColorLevel color,PaintedTerminalMessageRows *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::renderWrappedTemplate(message,templates,policy,maximumLines,color,out);
}
bool layoutTerminalEnergyAuthorizationPanelMessage(const TerminalEnergyAuthorizationPanelMessageRequest &request,const TerminalTextPolicy &policy,TerminalMessageLine *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::layoutTemplate(message,templates,policy,out);
}
bool wrapTerminalEnergyAuthorizationPanelMessage(const TerminalEnergyAuthorizationPanelMessageRequest &request,const TerminalTextPolicy &policy,std::size_t maximumLines,TerminalMessageRows *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::wrapTemplate(message,templates,policy,maximumLines,out);
}
bool renderTerminalEnergyAuthorizationPanelMessage(const TerminalEnergyAuthorizationPanelMessageRequest &request,const TerminalTextPolicy &policy,ColorLevel color,PaintedTerminalMessageLine *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::renderTemplate(message,templates,policy,color,out);
}
bool renderWrappedTerminalEnergyAuthorizationPanelMessage(const TerminalEnergyAuthorizationPanelMessageRequest &request,const TerminalTextPolicy &policy,std::size_t maximumLines,ColorLevel color,PaintedTerminalMessageRows *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::renderWrappedTemplate(message,templates,policy,maximumLines,color,out);
}
bool layoutTerminalEnergyAuthorizationValueMessage(const TerminalEnergyAuthorizationValueMessageRequest &request,const TerminalTextPolicy &policy,TerminalMessageLine *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::layoutTemplate(message,templates,policy,out);
}
bool wrapTerminalEnergyAuthorizationValueMessage(const TerminalEnergyAuthorizationValueMessageRequest &request,const TerminalTextPolicy &policy,std::size_t maximumLines,TerminalMessageRows *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::wrapTemplate(message,templates,policy,maximumLines,out);
}
bool renderTerminalEnergyAuthorizationValueMessage(const TerminalEnergyAuthorizationValueMessageRequest &request,const TerminalTextPolicy &policy,ColorLevel color,PaintedTerminalMessageLine *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::renderTemplate(message,templates,policy,color,out);
}
bool renderWrappedTerminalEnergyAuthorizationValueMessage(const TerminalEnergyAuthorizationValueMessageRequest &request,const TerminalTextPolicy &policy,std::size_t maximumLines,ColorLevel color,PaintedTerminalMessageRows *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::renderWrappedTemplate(message,templates,policy,maximumLines,color,out);
}
bool layoutTerminalEnergyAuthorizationPrivateMessage(const TerminalEnergyAuthorizationPrivateMessageRequest &request,const TerminalTextPolicy &policy,TerminalMessageLine *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::layoutTemplate(message,templates,policy,out);
}
bool wrapTerminalEnergyAuthorizationPrivateMessage(const TerminalEnergyAuthorizationPrivateMessageRequest &request,const TerminalTextPolicy &policy,std::size_t maximumLines,TerminalMessageRows *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::wrapTemplate(message,templates,policy,maximumLines,out);
}
bool renderTerminalEnergyAuthorizationPrivateMessage(const TerminalEnergyAuthorizationPrivateMessageRequest &request,const TerminalTextPolicy &policy,ColorLevel color,PaintedTerminalMessageLine *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::renderTemplate(message,templates,policy,color,out);
}
bool renderWrappedTerminalEnergyAuthorizationPrivateMessage(const TerminalEnergyAuthorizationPrivateMessageRequest &request,const TerminalTextPolicy &policy,std::size_t maximumLines,ColorLevel color,PaintedTerminalMessageRows *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::renderWrappedTemplate(message,templates,policy,maximumLines,color,out);
}
bool layoutTerminalTariffNoticeMessage(const TerminalTariffNoticeMessageRequest &request,const TerminalTextPolicy &policy,TerminalMessageLine *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::layoutTemplate(message,templates,policy,out);
}
bool wrapTerminalTariffNoticeMessage(const TerminalTariffNoticeMessageRequest &request,const TerminalTextPolicy &policy,std::size_t maximumLines,TerminalMessageRows *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::wrapTemplate(message,templates,policy,maximumLines,out);
}
bool renderTerminalTariffNoticeMessage(const TerminalTariffNoticeMessageRequest &request,const TerminalTextPolicy &policy,ColorLevel color,PaintedTerminalMessageLine *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::renderTemplate(message,templates,policy,color,out);
}
bool renderWrappedTerminalTariffNoticeMessage(const TerminalTariffNoticeMessageRequest &request,const TerminalTextPolicy &policy,std::size_t maximumLines,ColorLevel color,PaintedTerminalMessageRows *out){
    TerminalMessageRequest message;terminal_message_detail::Templates templates;return out&&prepare(request,message,templates)&&terminal_message_detail::renderWrappedTemplate(message,templates,policy,maximumLines,color,out);
}
} // namespace om
