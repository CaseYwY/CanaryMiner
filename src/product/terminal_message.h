// Typed named message arguments and whole-template recovery; no ordinary UI, protocol or source activation.
#pragma once
#include "localization.h"
#include "terminal_segments.h"
#include <optional>
namespace om {
struct TerminalMessageArgument {std::string name;TerminalSegment value;};
struct TerminalMessageRequest {
    std::string language="en";
    i18n::MessageKey key=i18n::MessageKey::Save;
    Tone literalTone=Tone::Plain;
    std::vector<TerminalMessageArgument> arguments;
};
struct TerminalMessageMetadata {
    std::string requestedLanguage,renderedLanguage;
    std::optional<i18n::ReviewStatus> requestedReview; // unknown requested language has no invented review status
    i18n::ReviewStatus renderedReview=i18n::ReviewStatus::Source;
    bool languageFallback=false,asciiRecovery=false,backendFallback=false,rtl=false;
    std::vector<std::string> sources; // source-span index -> original argument name; empty means catalog literal
};
struct TerminalMessageLine {ColoredTerminalLine line;TerminalMessageMetadata metadata;};
struct TerminalMessageRows {std::vector<ColoredTerminalLine> rows;TerminalMessageMetadata metadata;};
struct PaintedTerminalMessageLine {std::string text;TerminalMessageMetadata metadata;};
struct PaintedTerminalMessageRows {std::vector<std::string> rows;TerminalMessageMetadata metadata;};
// Select the complete English template before composition on explicit ASCII/backend-disabled recovery.
// Arguments preserve their types/tones, are never reparsed as templates, and must have exact unique names.
bool layoutTerminalMessage(const TerminalMessageRequest &,const TerminalTextPolicy &,TerminalMessageLine *);
bool wrapTerminalMessage(const TerminalMessageRequest &,const TerminalTextPolicy &,std::size_t maximumLines,TerminalMessageRows *);
bool renderTerminalMessage(const TerminalMessageRequest &,const TerminalTextPolicy &,ColorLevel,PaintedTerminalMessageLine *);
bool renderWrappedTerminalMessage(const TerminalMessageRequest &,const TerminalTextPolicy &,std::size_t maximumLines,ColorLevel,PaintedTerminalMessageRows *);
} // namespace om
