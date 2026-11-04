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
enum class TerminalTextAtom { None, LeftToRight }; // atomic human presentation, NOT a byte-invariant machine identity
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
    TerminalTextAtom atom=TerminalTextAtom::None;
    TerminalTextSpan(std::string logical,std::string fallback={},bool stable=false,TerminalTextAtom atomic=TerminalTextAtom::None):text(std::move(logical)),ascii(std::move(fallback)),invariant(stable),atom(atomic){}
};
struct TerminalTextLine {
    std::string text;
    std::size_t cells=0;
    bool clipped=false,replacedOrphan=false;
    TerminalTextFallback fallback=TerminalTextFallback::None;
};
struct TerminalTextBackend {bool unicode=false;std::string implementation,version,unicodeVersion;};
enum class TerminalTextAlignment { Left, Right };
TerminalTextBackend terminalTextBackend();
// At most 64 spans/4096 input bytes, 4096 cells, 8192 output bytes. Failure preserves the output.
// Span edges must be grapheme boundaries. Logical output isolates an explicit paragraph direction and invariant spans;
// Visual output removes them and must NOT be sent to an auto-bidi terminal. Neither mode shapes glyphs.
bool layoutTerminalText(const std::vector<TerminalTextSpan> &,const TerminalTextPolicy &,TerminalTextLine *);
// Complete autonomous display rows, no dropped whitespace/identity. ICU root line opportunities when enabled, ASCII
// space opportunities for fallback. Too-wide graphemes/atomic identities, row/output limits fail transactionally.
// Long human words may break only at grapheme edges. Logical rows are independent bidi paragraphs;
// they are NOT a rich-text paragraph's preserved cross-line bidi context. 1..4096 rows; 32768 output bytes.
bool wrapTerminalText(const std::vector<TerminalTextSpan> &,const TerminalTextPolicy &,std::size_t maximumLines,std::vector<TerminalTextLine> *);
// Pad the clipped line to exactly policy.columns cells. Logical mode isolates a physical LTR field
// around an FSI-isolated content block; it is render-only, never a machine/identity string.
bool padTerminalText(const std::vector<TerminalTextSpan> &,const TerminalTextPolicy &,TerminalTextAlignment,TerminalTextLine *);
// UTF-8 byte offsets (including 0/end), maintained UAX #29 when enabled. No terminal-control safety claim.
// General conformance input may include controls; layoutTerminalText is the stricter render boundary.
bool terminalGraphemeBoundaries(std::string_view,std::vector<std::size_t> *);
} // namespace om
