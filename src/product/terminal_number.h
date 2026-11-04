// A display adapter only: no monetary source, asset/currency inference, conversion or mining authority.
#pragma once
#include "localized_format.h"
#include "terminal_segments.h"
namespace om {
struct TerminalQuantityRequest {PresentationNumberRequest number;std::string label;Tone tone=Tone::Plain;};
struct TerminalQuantity {
    PresentationNumber number; // exact ASCII value, explicit rounding/fallback/plural/resource metadata retained
    TerminalSegment segment{TerminalTextSpan{""}}; // entire number + explicit ASCII label, atomic human LTR presentation
};
// Label is empty or one exact 1..32-byte ASCII unit/asset token, never translated/inferred from locale.
// Reuses exact number formatting; invalid request/label/tone or backend failure preserves output.
bool formatTerminalQuantity(const TerminalQuantityRequest &,TerminalQuantity *);
} // namespace om
