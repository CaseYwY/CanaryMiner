// Typed root-settings domains; ordinary native settings still binds English source, never draft activation.
#pragma once
#include "settings_messages.h"
#include "ui_field_messages.h"
#include "settings_change_messages.h"
#include "settings_action_messages.h"
#include "settings_diagnostic_messages.h"
#include "language_field_messages.h"
#include "language_panel_messages.h"
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
} // namespace om
