// Typed root-settings domains; ordinary native settings still binds English source, never draft activation.
#pragma once
#include "settings_messages.h"
#include "ui_field_messages.h"
#include "settings_change_messages.h"
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
} // namespace om
