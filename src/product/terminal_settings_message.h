// Typed root-settings domains; ordinary native settings still binds English source, never draft activation.
#pragma once
#include "settings_messages.h"
#include "ui_field_messages.h"
#include "settings_change_messages.h"
#include "settings_action_messages.h"
#include "settings_diagnostic_messages.h"
#include "language_field_messages.h"
#include "language_panel_messages.h"
#include "runtime_field_messages.h"
#include "runtime_panel_messages.h"
#include "activation_messages.h"
#include "game_field_messages.h"
#include "game_panel_messages.h"
#include "game_value_messages.h"
#include "pool_field_messages.h"
#include "pool_notice_messages.h"
#include "pool_panel_messages.h"
#include "pool_value_messages.h"
#include "pool_rule_messages.h"
#include "manual_market_field_messages.h"
#include "manual_market_notice_messages.h"
#include "manual_market_panel_messages.h"
#include "manual_market_value_messages.h"
#include "manual_market_context_messages.h"
#include "electricity_field_messages.h"
#include "electricity_notice_messages.h"
#include "electricity_panel_messages.h"
#include "electricity_value_messages.h"
#include "electricity_context_messages.h"
#include "electricity_tariff_messages.h"
#include "energy_authorization_field_messages.h"
#include "energy_authorization_notice_messages.h"
#include "energy_authorization_panel_messages.h"
#include "terminal_message.h"
namespace om {
struct TerminalSettingsMessageRequest {
    std::string language="en";
    i18n::SettingsMessageKey key=i18n::SettingsMessageKey::Title;
    Tone literalTone=Tone::Plain;
    std::vector<TerminalMessageArgument> arguments;
};
bool layoutTerminalSettingsMessage(const TerminalSettingsMessageRequest &,const TerminalTextPolicy &,TerminalMessageLine *);
bool wrapTerminalSettingsMessage(const TerminalSettingsMessageRequest &,const TerminalTextPolicy &,std::size_t,TerminalMessageRows *);
bool renderTerminalSettingsMessage(const TerminalSettingsMessageRequest &,const TerminalTextPolicy &,ColorLevel,PaintedTerminalMessageLine *);
bool renderWrappedTerminalSettingsMessage(const TerminalSettingsMessageRequest &,const TerminalTextPolicy &,std::size_t,ColorLevel,PaintedTerminalMessageRows *);
struct TerminalUiFieldMessageRequest {
    std::string language="en";
    i18n::UiFieldMessageKey key=i18n::UiFieldMessageKey::Refresh;
    Tone literalTone=Tone::Plain;
    std::vector<TerminalMessageArgument> arguments;
};
bool layoutTerminalUiFieldMessage(const TerminalUiFieldMessageRequest &,const TerminalTextPolicy &,TerminalMessageLine *);
bool wrapTerminalUiFieldMessage(const TerminalUiFieldMessageRequest &,const TerminalTextPolicy &,std::size_t,TerminalMessageRows *);
bool renderTerminalUiFieldMessage(const TerminalUiFieldMessageRequest &,const TerminalTextPolicy &,ColorLevel,PaintedTerminalMessageLine *);
bool renderWrappedTerminalUiFieldMessage(const TerminalUiFieldMessageRequest &,const TerminalTextPolicy &,std::size_t,ColorLevel,PaintedTerminalMessageRows *);
struct TerminalSettingsChangeMessageRequest {
    std::string language="en";
    i18n::SettingsChangeMessageKey key=i18n::SettingsChangeMessageKey::Color;
    Tone literalTone=Tone::Plain;
    std::vector<TerminalMessageArgument> arguments;
};
bool layoutTerminalSettingsChangeMessage(const TerminalSettingsChangeMessageRequest &,const TerminalTextPolicy &,TerminalMessageLine *);
bool wrapTerminalSettingsChangeMessage(const TerminalSettingsChangeMessageRequest &,const TerminalTextPolicy &,std::size_t,TerminalMessageRows *);
bool renderTerminalSettingsChangeMessage(const TerminalSettingsChangeMessageRequest &,const TerminalTextPolicy &,ColorLevel,PaintedTerminalMessageLine *);
bool renderWrappedTerminalSettingsChangeMessage(const TerminalSettingsChangeMessageRequest &,const TerminalTextPolicy &,std::size_t,ColorLevel,PaintedTerminalMessageRows *);
struct TerminalSettingsActionMessageRequest {
    std::string language="en";
    i18n::SettingsActionMessageKey key=i18n::SettingsActionMessageKey::LanguageEffective;
    Tone literalTone=Tone::Plain;
    std::vector<TerminalMessageArgument> arguments;
};
bool layoutTerminalSettingsActionMessage(const TerminalSettingsActionMessageRequest &,const TerminalTextPolicy &,TerminalMessageLine *);
bool wrapTerminalSettingsActionMessage(const TerminalSettingsActionMessageRequest &,const TerminalTextPolicy &,std::size_t,TerminalMessageRows *);
bool renderTerminalSettingsActionMessage(const TerminalSettingsActionMessageRequest &,const TerminalTextPolicy &,ColorLevel,PaintedTerminalMessageLine *);
bool renderWrappedTerminalSettingsActionMessage(const TerminalSettingsActionMessageRequest &,const TerminalTextPolicy &,std::size_t,ColorLevel,PaintedTerminalMessageRows *);
struct TerminalSettingsDiagnosticMessageRequest {
    std::string language="en";
    i18n::SettingsDiagnosticMessageKey key=i18n::SettingsDiagnosticMessageKey::RequiresTerminal;
    Tone literalTone=Tone::Plain;
    std::vector<TerminalMessageArgument> arguments;
};
bool layoutTerminalSettingsDiagnosticMessage(const TerminalSettingsDiagnosticMessageRequest &,const TerminalTextPolicy &,TerminalMessageLine *);
bool wrapTerminalSettingsDiagnosticMessage(const TerminalSettingsDiagnosticMessageRequest &,const TerminalTextPolicy &,std::size_t,TerminalMessageRows *);
bool renderTerminalSettingsDiagnosticMessage(const TerminalSettingsDiagnosticMessageRequest &,const TerminalTextPolicy &,ColorLevel,PaintedTerminalMessageLine *);
bool renderWrappedTerminalSettingsDiagnosticMessage(const TerminalSettingsDiagnosticMessageRequest &,const TerminalTextPolicy &,std::size_t,ColorLevel,PaintedTerminalMessageRows *);
struct TerminalLanguageFieldMessageRequest {
    std::string language="en";
    i18n::LanguageFieldMessageKey key=i18n::LanguageFieldMessageKey::LanguageId;
    Tone literalTone=Tone::Plain;
};
bool layoutTerminalLanguageFieldMessage(const TerminalLanguageFieldMessageRequest &,const TerminalTextPolicy &,TerminalMessageLine *);
bool wrapTerminalLanguageFieldMessage(const TerminalLanguageFieldMessageRequest &,const TerminalTextPolicy &,std::size_t,TerminalMessageRows *);
bool renderTerminalLanguageFieldMessage(const TerminalLanguageFieldMessageRequest &,const TerminalTextPolicy &,ColorLevel,PaintedTerminalMessageLine *);
bool renderWrappedTerminalLanguageFieldMessage(const TerminalLanguageFieldMessageRequest &,const TerminalTextPolicy &,std::size_t,ColorLevel,PaintedTerminalMessageRows *);
struct TerminalLanguagePanelMessageRequest {
    std::string language="en";
    i18n::LanguagePanelMessageKey key=i18n::LanguagePanelMessageKey::Title;
    Tone literalTone=Tone::Plain;
    std::vector<TerminalMessageArgument> arguments;
};
bool layoutTerminalLanguagePanelMessage(const TerminalLanguagePanelMessageRequest &,const TerminalTextPolicy &,TerminalMessageLine *);
bool wrapTerminalLanguagePanelMessage(const TerminalLanguagePanelMessageRequest &,const TerminalTextPolicy &,std::size_t,TerminalMessageRows *);
bool renderTerminalLanguagePanelMessage(const TerminalLanguagePanelMessageRequest &,const TerminalTextPolicy &,ColorLevel,PaintedTerminalMessageLine *);
bool renderWrappedTerminalLanguagePanelMessage(const TerminalLanguagePanelMessageRequest &,const TerminalTextPolicy &,std::size_t,ColorLevel,PaintedTerminalMessageRows *);
struct TerminalRuntimeFieldMessageRequest {
    std::string language="en";
    i18n::RuntimeFieldMessageKey key=i18n::RuntimeFieldMessageKey::WaitConfigured;
    Tone literalTone=Tone::Plain;
    std::vector<TerminalMessageArgument> arguments;
};
bool layoutTerminalRuntimeFieldMessage(const TerminalRuntimeFieldMessageRequest &,const TerminalTextPolicy &,TerminalMessageLine *);
bool wrapTerminalRuntimeFieldMessage(const TerminalRuntimeFieldMessageRequest &,const TerminalTextPolicy &,std::size_t,TerminalMessageRows *);
bool renderTerminalRuntimeFieldMessage(const TerminalRuntimeFieldMessageRequest &,const TerminalTextPolicy &,ColorLevel,PaintedTerminalMessageLine *);
bool renderWrappedTerminalRuntimeFieldMessage(const TerminalRuntimeFieldMessageRequest &,const TerminalTextPolicy &,std::size_t,ColorLevel,PaintedTerminalMessageRows *);
struct TerminalRuntimePanelMessageRequest {
    std::string language="en";
    i18n::RuntimePanelMessageKey key=i18n::RuntimePanelMessageKey::TitleFull;
    Tone literalTone=Tone::Plain;
    std::vector<TerminalMessageArgument> arguments;
};
bool layoutTerminalRuntimePanelMessage(const TerminalRuntimePanelMessageRequest &,const TerminalTextPolicy &,TerminalMessageLine *);
bool wrapTerminalRuntimePanelMessage(const TerminalRuntimePanelMessageRequest &,const TerminalTextPolicy &,std::size_t,TerminalMessageRows *);
bool renderTerminalRuntimePanelMessage(const TerminalRuntimePanelMessageRequest &,const TerminalTextPolicy &,ColorLevel,PaintedTerminalMessageLine *);
bool renderWrappedTerminalRuntimePanelMessage(const TerminalRuntimePanelMessageRequest &,const TerminalTextPolicy &,std::size_t,ColorLevel,PaintedTerminalMessageRows *);
struct TerminalActivationMessageRequest {
    std::string language="en";
    i18n::ActivationMessageKey key=i18n::ActivationMessageKey::NextStart;
    Tone literalTone=Tone::Plain;
};
bool layoutTerminalActivationMessage(const TerminalActivationMessageRequest &,const TerminalTextPolicy &,TerminalMessageLine *);
bool wrapTerminalActivationMessage(const TerminalActivationMessageRequest &,const TerminalTextPolicy &,std::size_t,TerminalMessageRows *);
bool renderTerminalActivationMessage(const TerminalActivationMessageRequest &,const TerminalTextPolicy &,ColorLevel,PaintedTerminalMessageLine *);
bool renderWrappedTerminalActivationMessage(const TerminalActivationMessageRequest &,const TerminalTextPolicy &,std::size_t,ColorLevel,PaintedTerminalMessageRows *);
struct TerminalGameFieldMessageRequest {
    std::string language="en";
    i18n::GameFieldMessageKey key=i18n::GameFieldMessageKey::Mode;
    Tone literalTone=Tone::Plain;
};
bool layoutTerminalGameFieldMessage(const TerminalGameFieldMessageRequest &,const TerminalTextPolicy &,TerminalMessageLine *);
bool wrapTerminalGameFieldMessage(const TerminalGameFieldMessageRequest &,const TerminalTextPolicy &,std::size_t,TerminalMessageRows *);
bool renderTerminalGameFieldMessage(const TerminalGameFieldMessageRequest &,const TerminalTextPolicy &,ColorLevel,PaintedTerminalMessageLine *);
bool renderWrappedTerminalGameFieldMessage(const TerminalGameFieldMessageRequest &,const TerminalTextPolicy &,std::size_t,ColorLevel,PaintedTerminalMessageRows *);
struct TerminalGamePanelMessageRequest {
    std::string language="en";
    i18n::GamePanelMessageKey key=i18n::GamePanelMessageKey::Title;
    Tone literalTone=Tone::Plain;
};
bool layoutTerminalGamePanelMessage(const TerminalGamePanelMessageRequest &,const TerminalTextPolicy &,TerminalMessageLine *);
bool wrapTerminalGamePanelMessage(const TerminalGamePanelMessageRequest &,const TerminalTextPolicy &,std::size_t,TerminalMessageRows *);
bool renderTerminalGamePanelMessage(const TerminalGamePanelMessageRequest &,const TerminalTextPolicy &,ColorLevel,PaintedTerminalMessageLine *);
bool renderWrappedTerminalGamePanelMessage(const TerminalGamePanelMessageRequest &,const TerminalTextPolicy &,std::size_t,ColorLevel,PaintedTerminalMessageRows *);
struct TerminalGameValueMessageRequest {
    std::string language="en";
    i18n::GameValueMessageKey key=i18n::GameValueMessageKey::FieldValue;
    Tone literalTone=Tone::Plain;
    std::vector<TerminalMessageArgument> arguments;
};
bool layoutTerminalGameValueMessage(const TerminalGameValueMessageRequest &,const TerminalTextPolicy &,TerminalMessageLine *);
bool wrapTerminalGameValueMessage(const TerminalGameValueMessageRequest &,const TerminalTextPolicy &,std::size_t,TerminalMessageRows *);
bool renderTerminalGameValueMessage(const TerminalGameValueMessageRequest &,const TerminalTextPolicy &,ColorLevel,PaintedTerminalMessageLine *);
bool renderWrappedTerminalGameValueMessage(const TerminalGameValueMessageRequest &,const TerminalTextPolicy &,std::size_t,ColorLevel,PaintedTerminalMessageRows *);
struct TerminalPoolFieldMessageRequest {
    std::string language="en";
    i18n::PoolFieldMessageKey key=i18n::PoolFieldMessageKey::Scope;
    Tone literalTone=Tone::Plain;
};
bool layoutTerminalPoolFieldMessage(const TerminalPoolFieldMessageRequest &,const TerminalTextPolicy &,TerminalMessageLine *);
bool wrapTerminalPoolFieldMessage(const TerminalPoolFieldMessageRequest &,const TerminalTextPolicy &,std::size_t,TerminalMessageRows *);
bool renderTerminalPoolFieldMessage(const TerminalPoolFieldMessageRequest &,const TerminalTextPolicy &,ColorLevel,PaintedTerminalMessageLine *);
bool renderWrappedTerminalPoolFieldMessage(const TerminalPoolFieldMessageRequest &,const TerminalTextPolicy &,std::size_t,ColorLevel,PaintedTerminalMessageRows *);
struct TerminalPoolNoticeMessageRequest {
    std::string language="en";
    i18n::PoolNoticeMessageKey key=i18n::PoolNoticeMessageKey::DraftChanged;
    Tone literalTone=Tone::Plain;
};
bool layoutTerminalPoolNoticeMessage(const TerminalPoolNoticeMessageRequest &,const TerminalTextPolicy &,TerminalMessageLine *);
bool wrapTerminalPoolNoticeMessage(const TerminalPoolNoticeMessageRequest &,const TerminalTextPolicy &,std::size_t,TerminalMessageRows *);
bool renderTerminalPoolNoticeMessage(const TerminalPoolNoticeMessageRequest &,const TerminalTextPolicy &,ColorLevel,PaintedTerminalMessageLine *);
bool renderWrappedTerminalPoolNoticeMessage(const TerminalPoolNoticeMessageRequest &,const TerminalTextPolicy &,std::size_t,ColorLevel,PaintedTerminalMessageRows *);
struct TerminalPoolPanelMessageRequest {
    std::string language="en";
    i18n::PoolPanelMessageKey key=i18n::PoolPanelMessageKey::Title;
    Tone literalTone=Tone::Plain;
};
bool layoutTerminalPoolPanelMessage(const TerminalPoolPanelMessageRequest &,const TerminalTextPolicy &,TerminalMessageLine *);
bool wrapTerminalPoolPanelMessage(const TerminalPoolPanelMessageRequest &,const TerminalTextPolicy &,std::size_t,TerminalMessageRows *);
bool renderTerminalPoolPanelMessage(const TerminalPoolPanelMessageRequest &,const TerminalTextPolicy &,ColorLevel,PaintedTerminalMessageLine *);
bool renderWrappedTerminalPoolPanelMessage(const TerminalPoolPanelMessageRequest &,const TerminalTextPolicy &,std::size_t,ColorLevel,PaintedTerminalMessageRows *);
struct TerminalPoolValueMessageRequest {
    std::string language="en";
    i18n::PoolValueMessageKey key=i18n::PoolValueMessageKey::FieldValue;
    Tone literalTone=Tone::Plain;
    std::vector<TerminalMessageArgument> arguments;
};
bool layoutTerminalPoolValueMessage(const TerminalPoolValueMessageRequest &,const TerminalTextPolicy &,TerminalMessageLine *);
bool wrapTerminalPoolValueMessage(const TerminalPoolValueMessageRequest &,const TerminalTextPolicy &,std::size_t,TerminalMessageRows *);
bool renderTerminalPoolValueMessage(const TerminalPoolValueMessageRequest &,const TerminalTextPolicy &,ColorLevel,PaintedTerminalMessageLine *);
bool renderWrappedTerminalPoolValueMessage(const TerminalPoolValueMessageRequest &,const TerminalTextPolicy &,std::size_t,ColorLevel,PaintedTerminalMessageRows *);
struct TerminalPoolRuleMessageRequest {
    std::string language="en";
    i18n::PoolRuleMessageKey key=i18n::PoolRuleMessageKey::BeforeUnset;
    Tone literalTone=Tone::Plain;
    std::vector<TerminalMessageArgument> arguments;
};
bool layoutTerminalPoolRuleMessage(const TerminalPoolRuleMessageRequest &,const TerminalTextPolicy &,TerminalMessageLine *);
bool wrapTerminalPoolRuleMessage(const TerminalPoolRuleMessageRequest &,const TerminalTextPolicy &,std::size_t,TerminalMessageRows *);
bool renderTerminalPoolRuleMessage(const TerminalPoolRuleMessageRequest &,const TerminalTextPolicy &,ColorLevel,PaintedTerminalMessageLine *);
bool renderWrappedTerminalPoolRuleMessage(const TerminalPoolRuleMessageRequest &,const TerminalTextPolicy &,std::size_t,ColorLevel,PaintedTerminalMessageRows *);
struct TerminalManualMarketFieldMessageRequest {
    std::string language="en";
    i18n::ManualMarketFieldMessageKey key=i18n::ManualMarketFieldMessageKey::DisplayEnabled;
    Tone literalTone=Tone::Plain;
};
bool layoutTerminalManualMarketFieldMessage(const TerminalManualMarketFieldMessageRequest &,const TerminalTextPolicy &,TerminalMessageLine *);
bool wrapTerminalManualMarketFieldMessage(const TerminalManualMarketFieldMessageRequest &,const TerminalTextPolicy &,std::size_t,TerminalMessageRows *);
bool renderTerminalManualMarketFieldMessage(const TerminalManualMarketFieldMessageRequest &,const TerminalTextPolicy &,ColorLevel,PaintedTerminalMessageLine *);
bool renderWrappedTerminalManualMarketFieldMessage(const TerminalManualMarketFieldMessageRequest &,const TerminalTextPolicy &,std::size_t,ColorLevel,PaintedTerminalMessageRows *);
struct TerminalManualMarketNoticeMessageRequest {
    std::string language="en";
    i18n::ManualMarketNoticeMessageKey key=i18n::ManualMarketNoticeMessageKey::InvalidDecimal;
    Tone literalTone=Tone::Plain;
};
bool layoutTerminalManualMarketNoticeMessage(const TerminalManualMarketNoticeMessageRequest &,const TerminalTextPolicy &,TerminalMessageLine *);
bool wrapTerminalManualMarketNoticeMessage(const TerminalManualMarketNoticeMessageRequest &,const TerminalTextPolicy &,std::size_t,TerminalMessageRows *);
bool renderTerminalManualMarketNoticeMessage(const TerminalManualMarketNoticeMessageRequest &,const TerminalTextPolicy &,ColorLevel,PaintedTerminalMessageLine *);
bool renderWrappedTerminalManualMarketNoticeMessage(const TerminalManualMarketNoticeMessageRequest &,const TerminalTextPolicy &,std::size_t,ColorLevel,PaintedTerminalMessageRows *);
struct TerminalManualMarketPanelMessageRequest {
    std::string language="en";
    i18n::ManualMarketPanelMessageKey key=i18n::ManualMarketPanelMessageKey::Title;
    Tone literalTone=Tone::Plain;
};
bool layoutTerminalManualMarketPanelMessage(const TerminalManualMarketPanelMessageRequest &,const TerminalTextPolicy &,TerminalMessageLine *);
bool wrapTerminalManualMarketPanelMessage(const TerminalManualMarketPanelMessageRequest &,const TerminalTextPolicy &,std::size_t,TerminalMessageRows *);
bool renderTerminalManualMarketPanelMessage(const TerminalManualMarketPanelMessageRequest &,const TerminalTextPolicy &,ColorLevel,PaintedTerminalMessageLine *);
bool renderWrappedTerminalManualMarketPanelMessage(const TerminalManualMarketPanelMessageRequest &,const TerminalTextPolicy &,std::size_t,ColorLevel,PaintedTerminalMessageRows *);
struct TerminalManualMarketValueMessageRequest {
    std::string language="en";
    i18n::ManualMarketValueMessageKey key=i18n::ManualMarketValueMessageKey::FieldValue;
    Tone literalTone=Tone::Plain;
    std::vector<TerminalMessageArgument> arguments;
};
bool layoutTerminalManualMarketValueMessage(const TerminalManualMarketValueMessageRequest &,const TerminalTextPolicy &,TerminalMessageLine *);
bool wrapTerminalManualMarketValueMessage(const TerminalManualMarketValueMessageRequest &,const TerminalTextPolicy &,std::size_t,TerminalMessageRows *);
bool renderTerminalManualMarketValueMessage(const TerminalManualMarketValueMessageRequest &,const TerminalTextPolicy &,ColorLevel,PaintedTerminalMessageLine *);
bool renderWrappedTerminalManualMarketValueMessage(const TerminalManualMarketValueMessageRequest &,const TerminalTextPolicy &,std::size_t,ColorLevel,PaintedTerminalMessageRows *);
struct TerminalManualMarketContextMessageRequest {
    std::string language="en";
    i18n::ManualMarketContextMessageKey key=i18n::ManualMarketContextMessageKey::CurrentDisplay;
    Tone literalTone=Tone::Plain;
    std::vector<TerminalMessageArgument> arguments;
};
bool layoutTerminalManualMarketContextMessage(const TerminalManualMarketContextMessageRequest &,const TerminalTextPolicy &,TerminalMessageLine *);
bool wrapTerminalManualMarketContextMessage(const TerminalManualMarketContextMessageRequest &,const TerminalTextPolicy &,std::size_t,TerminalMessageRows *);
bool renderTerminalManualMarketContextMessage(const TerminalManualMarketContextMessageRequest &,const TerminalTextPolicy &,ColorLevel,PaintedTerminalMessageLine *);
bool renderWrappedTerminalManualMarketContextMessage(const TerminalManualMarketContextMessageRequest &,const TerminalTextPolicy &,std::size_t,ColorLevel,PaintedTerminalMessageRows *);
struct TerminalElectricityFieldMessageRequest {
    std::string language="en";
    i18n::ElectricityFieldMessageKey key=i18n::ElectricityFieldMessageKey::CollectEnergy;
    Tone literalTone=Tone::Plain;
};
bool layoutTerminalElectricityFieldMessage(const TerminalElectricityFieldMessageRequest &,const TerminalTextPolicy &,TerminalMessageLine *);
bool wrapTerminalElectricityFieldMessage(const TerminalElectricityFieldMessageRequest &,const TerminalTextPolicy &,std::size_t,TerminalMessageRows *);
bool renderTerminalElectricityFieldMessage(const TerminalElectricityFieldMessageRequest &,const TerminalTextPolicy &,ColorLevel,PaintedTerminalMessageLine *);
bool renderWrappedTerminalElectricityFieldMessage(const TerminalElectricityFieldMessageRequest &,const TerminalTextPolicy &,std::size_t,ColorLevel,PaintedTerminalMessageRows *);
struct TerminalElectricityNoticeMessageRequest {
    std::string language="en";
    i18n::ElectricityNoticeMessageKey key=i18n::ElectricityNoticeMessageKey::InputTooLong;
    Tone literalTone=Tone::Plain;
};
bool layoutTerminalElectricityNoticeMessage(const TerminalElectricityNoticeMessageRequest &,const TerminalTextPolicy &,TerminalMessageLine *);
bool wrapTerminalElectricityNoticeMessage(const TerminalElectricityNoticeMessageRequest &,const TerminalTextPolicy &,std::size_t,TerminalMessageRows *);
bool renderTerminalElectricityNoticeMessage(const TerminalElectricityNoticeMessageRequest &,const TerminalTextPolicy &,ColorLevel,PaintedTerminalMessageLine *);
bool renderWrappedTerminalElectricityNoticeMessage(const TerminalElectricityNoticeMessageRequest &,const TerminalTextPolicy &,std::size_t,ColorLevel,PaintedTerminalMessageRows *);
struct TerminalElectricityPanelMessageRequest {
    std::string language="en";
    i18n::ElectricityPanelMessageKey key=i18n::ElectricityPanelMessageKey::Title;
    Tone literalTone=Tone::Plain;
};
bool layoutTerminalElectricityPanelMessage(const TerminalElectricityPanelMessageRequest &,const TerminalTextPolicy &,TerminalMessageLine *);
bool wrapTerminalElectricityPanelMessage(const TerminalElectricityPanelMessageRequest &,const TerminalTextPolicy &,std::size_t,TerminalMessageRows *);
bool renderTerminalElectricityPanelMessage(const TerminalElectricityPanelMessageRequest &,const TerminalTextPolicy &,ColorLevel,PaintedTerminalMessageLine *);
bool renderWrappedTerminalElectricityPanelMessage(const TerminalElectricityPanelMessageRequest &,const TerminalTextPolicy &,std::size_t,ColorLevel,PaintedTerminalMessageRows *);
struct TerminalElectricityValueMessageRequest {
    std::string language="en";
    i18n::ElectricityValueMessageKey key=i18n::ElectricityValueMessageKey::FieldValue;
    Tone literalTone=Tone::Plain;
    std::vector<TerminalMessageArgument> arguments;
};
bool layoutTerminalElectricityValueMessage(const TerminalElectricityValueMessageRequest &,const TerminalTextPolicy &,TerminalMessageLine *);
bool wrapTerminalElectricityValueMessage(const TerminalElectricityValueMessageRequest &,const TerminalTextPolicy &,std::size_t,TerminalMessageRows *);
bool renderTerminalElectricityValueMessage(const TerminalElectricityValueMessageRequest &,const TerminalTextPolicy &,ColorLevel,PaintedTerminalMessageLine *);
bool renderWrappedTerminalElectricityValueMessage(const TerminalElectricityValueMessageRequest &,const TerminalTextPolicy &,std::size_t,ColorLevel,PaintedTerminalMessageRows *);
struct TerminalElectricityContextMessageRequest {
    std::string language="en";
    i18n::ElectricityContextMessageKey key=i18n::ElectricityContextMessageKey::FieldPosition;
    Tone literalTone=Tone::Plain;
    std::vector<TerminalMessageArgument> arguments;
};
bool layoutTerminalElectricityContextMessage(const TerminalElectricityContextMessageRequest &,const TerminalTextPolicy &,TerminalMessageLine *);
bool wrapTerminalElectricityContextMessage(const TerminalElectricityContextMessageRequest &,const TerminalTextPolicy &,std::size_t,TerminalMessageRows *);
bool renderTerminalElectricityContextMessage(const TerminalElectricityContextMessageRequest &,const TerminalTextPolicy &,ColorLevel,PaintedTerminalMessageLine *);
bool renderWrappedTerminalElectricityContextMessage(const TerminalElectricityContextMessageRequest &,const TerminalTextPolicy &,std::size_t,ColorLevel,PaintedTerminalMessageRows *);
struct TerminalElectricityTariffMessageRequest {
    std::string language="en";
    i18n::ElectricityTariffMessageKey key=i18n::ElectricityTariffMessageKey::Off;
    Tone literalTone=Tone::Plain;
    std::vector<TerminalMessageArgument> arguments;
};
bool layoutTerminalElectricityTariffMessage(const TerminalElectricityTariffMessageRequest &,const TerminalTextPolicy &,TerminalMessageLine *);
bool wrapTerminalElectricityTariffMessage(const TerminalElectricityTariffMessageRequest &,const TerminalTextPolicy &,std::size_t,TerminalMessageRows *);
bool renderTerminalElectricityTariffMessage(const TerminalElectricityTariffMessageRequest &,const TerminalTextPolicy &,ColorLevel,PaintedTerminalMessageLine *);
bool renderWrappedTerminalElectricityTariffMessage(const TerminalElectricityTariffMessageRequest &,const TerminalTextPolicy &,std::size_t,ColorLevel,PaintedTerminalMessageRows *);
struct TerminalEnergyAuthorizationFieldMessageRequest {
    std::string language="en";
    i18n::EnergyAuthorizationFieldMessageKey key=i18n::EnergyAuthorizationFieldMessageKey::PersistHistory;
    Tone literalTone=Tone::Plain;
};
bool layoutTerminalEnergyAuthorizationFieldMessage(const TerminalEnergyAuthorizationFieldMessageRequest &,const TerminalTextPolicy &,TerminalMessageLine *);
bool wrapTerminalEnergyAuthorizationFieldMessage(const TerminalEnergyAuthorizationFieldMessageRequest &,const TerminalTextPolicy &,std::size_t,TerminalMessageRows *);
bool renderTerminalEnergyAuthorizationFieldMessage(const TerminalEnergyAuthorizationFieldMessageRequest &,const TerminalTextPolicy &,ColorLevel,PaintedTerminalMessageLine *);
bool renderWrappedTerminalEnergyAuthorizationFieldMessage(const TerminalEnergyAuthorizationFieldMessageRequest &,const TerminalTextPolicy &,std::size_t,ColorLevel,PaintedTerminalMessageRows *);
struct TerminalEnergyAuthorizationNoticeMessageRequest {
    std::string language="en";
    i18n::EnergyAuthorizationNoticeMessageKey key=i18n::EnergyAuthorizationNoticeMessageKey::OwnerChanged;
    Tone literalTone=Tone::Plain;
};
bool layoutTerminalEnergyAuthorizationNoticeMessage(const TerminalEnergyAuthorizationNoticeMessageRequest &,const TerminalTextPolicy &,TerminalMessageLine *);
bool wrapTerminalEnergyAuthorizationNoticeMessage(const TerminalEnergyAuthorizationNoticeMessageRequest &,const TerminalTextPolicy &,std::size_t,TerminalMessageRows *);
bool renderTerminalEnergyAuthorizationNoticeMessage(const TerminalEnergyAuthorizationNoticeMessageRequest &,const TerminalTextPolicy &,ColorLevel,PaintedTerminalMessageLine *);
bool renderWrappedTerminalEnergyAuthorizationNoticeMessage(const TerminalEnergyAuthorizationNoticeMessageRequest &,const TerminalTextPolicy &,std::size_t,ColorLevel,PaintedTerminalMessageRows *);
struct TerminalEnergyAuthorizationPanelMessageRequest {
    std::string language="en";
    i18n::EnergyAuthorizationPanelMessageKey key=i18n::EnergyAuthorizationPanelMessageKey::Title;
    Tone literalTone=Tone::Plain;
};
bool layoutTerminalEnergyAuthorizationPanelMessage(const TerminalEnergyAuthorizationPanelMessageRequest &,const TerminalTextPolicy &,TerminalMessageLine *);
bool wrapTerminalEnergyAuthorizationPanelMessage(const TerminalEnergyAuthorizationPanelMessageRequest &,const TerminalTextPolicy &,std::size_t,TerminalMessageRows *);
bool renderTerminalEnergyAuthorizationPanelMessage(const TerminalEnergyAuthorizationPanelMessageRequest &,const TerminalTextPolicy &,ColorLevel,PaintedTerminalMessageLine *);
bool renderWrappedTerminalEnergyAuthorizationPanelMessage(const TerminalEnergyAuthorizationPanelMessageRequest &,const TerminalTextPolicy &,std::size_t,ColorLevel,PaintedTerminalMessageRows *);
} // namespace om
