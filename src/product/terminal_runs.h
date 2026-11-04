// Render-only provenance from the same grapheme/cell/bidi pass, not substring-based style reconstruction.
#pragma once
#include "terminal_text.h"
namespace om {
struct TerminalTextRun {
    std::string text;
    std::size_t sourceSpan=0,cells=0;
    bool control=false; // only internally generated direction isolation; sourceSpan is unspecified here
};
struct TerminalTextRunLine {TerminalTextLine line;std::vector<TerminalTextRun> runs;};
// Same validation, clipping and bounds as layoutTerminalText. Runs concatenate exactly to line.text;
// their cells sum to line.cells. Color/control bytes must be added only after this layout boundary.
bool layoutTerminalTextRuns(const std::vector<TerminalTextSpan> &,const TerminalTextPolicy &,TerminalTextRunLine *);
} // namespace om
