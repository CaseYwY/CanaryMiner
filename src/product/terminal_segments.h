// Independent typed colored presentation; not ordinary dashboard or language activation.
#pragma once
#include "terminal_runs.h"
#include "terminal_style.h"
namespace om {
struct TerminalSegment {
    TerminalTextSpan content;
    Tone tone=Tone::Plain;
    TerminalSegment(TerminalTextSpan text,Tone style=Tone::Plain):content(std::move(text)),tone(style){}
};
struct ColoredTerminalRun {TerminalTextRun content;Tone tone=Tone::Plain;};
struct ColoredTerminalLine {TerminalTextLine line;std::vector<ColoredTerminalRun> runs;};
// Styles follow original span provenance even after visual reordering. Direction controls stay unstyled.
bool layoutTerminalSegments(const std::vector<TerminalSegment> &,const TerminalTextPolicy &,ColoredTerminalLine *);
// Uses the existing fixed palette after layout, no input escape interpretation or rule-glyph substitution.
// ColorLevel::None is byte-exact plain output; caller owns NO_COLOR/capability selection. Max 65536 bytes.
bool renderTerminalSegments(const std::vector<TerminalSegment> &,const TerminalTextPolicy &,ColorLevel,std::string *);
bool wrapTerminalSegments(const std::vector<TerminalSegment> &,const TerminalTextPolicy &,std::size_t maximumLines,std::vector<ColoredTerminalLine> *);
bool padTerminalSegments(const std::vector<TerminalSegment> &,const TerminalTextPolicy &,TerminalTextAlignment,ColoredTerminalLine *,Tone padding=Tone::Plain);
// Entire row group is transactional. Original 4096-row/32768-plain-byte limits apply; painted group <=262144 bytes.
bool renderWrappedTerminalSegments(const std::vector<TerminalSegment> &,const TerminalTextPolicy &,std::size_t maximumLines,ColorLevel,std::vector<std::string> *);
bool renderPaddedTerminalSegments(const std::vector<TerminalSegment> &,const TerminalTextPolicy &,TerminalTextAlignment,ColorLevel,std::string *,Tone padding=Tone::Plain);
} // namespace om
