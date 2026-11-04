// Bounded presentation text only. Never use these strings for protocol, identity, JSON or arithmetic.
#pragma once
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
namespace om {
enum class TerminalTextOrder { Ascii, Logical, Visual };
enum class TerminalTextDirection { Auto, LeftToRight, RightToLeft };
enum class TerminalTextFallback { None, RequestedAscii, BackendDisabled };
struct TerminalTextPolicy {
    TerminalTextOrder order=TerminalTextOrder::Ascii;
    TerminalTextDirection direction=TerminalTextDirection::Auto;
    unsigned ambiguousCells=1,emojiCells=2;
    std::size_t columns=80;
};
struct TerminalTextSpan {
    std::string text;
    std::string ascii; // explicit human fallback, not guessed transliteration; empty reuses printable ASCII text
    bool invariant=false; // printable ASCII, atomic under clipping, isolated LTR under bidi; not translated or mirrored
    TerminalTextSpan(std::string logical,std::string fallback={},bool stable=false):text(std::move(logical)),ascii(std::move(fallback)),invariant(stable){}
};
struct TerminalTextLine {
    std::string text;
    std::size_t cells=0;
    bool clipped=false,replacedOrphan=false;
    TerminalTextFallback fallback=TerminalTextFallback::None;
};
struct TerminalTextBackend {bool unicode=false;std::string implementation,version,unicodeVersion;};
TerminalTextBackend terminalTextBackend();
// At most 64 spans/4096 input bytes, 4096 cells, 8192 output bytes. Failure preserves the output.
// Span edges must be grapheme boundaries. Logical output isolates an explicit paragraph direction and invariant spans;
// Visual output removes them and must NOT be sent to an auto-bidi terminal. Neither mode shapes glyphs.
bool layoutTerminalText(const std::vector<TerminalTextSpan> &,const TerminalTextPolicy &,TerminalTextLine *);
// UTF-8 byte offsets (including 0/end), maintained UAX #29 when enabled. No terminal-control safety claim.
// General conformance input may include controls; layoutTerminalText is the stricter render boundary.
bool terminalGraphemeBoundaries(std::string_view,std::vector<std::size_t> *);
} // namespace om
