// Maintained complete count sentences. Missing counts remain invalid, never default zero or financial credit.
#pragma once
#include "count_messages.h"
#include "localized_format.h"
#include "terminal_message.h"
namespace om {
struct TerminalCountRequest {
    i18n::CountMessageKey key=i18n::CountMessageKey::Workers;
    std::string count,language="en",formattingLocale="en";
    PresentationDigits digits=PresentationDigits::Latin;
    bool grouping=true;
    Tone countTone=Tone::Canary,literalTone=Tone::Plain;
};
struct TerminalCountLine {TerminalMessageLine message;PresentationNumber number;bool neutral=false;};
struct TerminalCountRows {TerminalMessageRows message;PresentationNumber number;bool neutral=false;};
struct PaintedTerminalCountLine {PaintedTerminalMessageLine message;PresentationNumber number;bool neutral=false;};
struct PaintedTerminalCountRows {PaintedTerminalMessageRows message;PresentationNumber number;bool neutral=false;};
// Canonical nonnegative integer input only. English neutral headers are used when plural rules are unavailable;
// no guessed category or hand-written numeric language rule is substituted. All output is transactional.
bool layoutTerminalCount(const TerminalCountRequest &,const TerminalTextPolicy &,TerminalCountLine *);
bool wrapTerminalCount(const TerminalCountRequest &,const TerminalTextPolicy &,std::size_t maximumLines,TerminalCountRows *);
bool renderTerminalCount(const TerminalCountRequest &,const TerminalTextPolicy &,ColorLevel,PaintedTerminalCountLine *);
bool renderWrappedTerminalCount(const TerminalCountRequest &,const TerminalTextPolicy &,std::size_t maximumLines,ColorLevel,PaintedTerminalCountRows *);
} // namespace om
