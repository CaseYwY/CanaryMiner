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
} // namespace om
