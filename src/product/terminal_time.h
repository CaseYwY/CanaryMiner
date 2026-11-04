// Display-only timestamps from an explicit civil clock. No system clock/zone, billing reset or protocol mutation.
#pragma once
#include "civil_time.h"
#include "localized_format.h"
#include "terminal_segments.h"
namespace om {
enum class PresentationTimeFields { DateTime, Date, Time };
enum class PresentationDateStyle { Numeric, AbbreviatedMonth };
enum class PresentationHourCycle { Hour24, Hour12 };
struct TerminalTimeRequest {
    std::optional<int64_t> utcMs; // missing is not the epoch or 'now'
    CivilClock clock;
    std::string formattingLocale="en";
    PresentationDigits digits=PresentationDigits::Latin;
    PresentationTimeFields fields=PresentationTimeFields::DateTime;
    PresentationDateStyle dateStyle=PresentationDateStyle::Numeric;
    PresentationHourCycle hours=PresentationHourCycle::Hour24;
    bool milliseconds=true,asciiOnly=false;
    Tone tone=Tone::Plain;
};
struct TerminalTime {
    int64_t utcMs=0;
    CivilClock clock;
    CivilStamp civil; // original source clock/version/offset; never replaced by ICU timezone data
    std::string text,ascii,zoneLabel,formattingLocale;
    bool backendFallback=false,localeFallback=false,resourceInheritance=false,asciiRecovery=false;
    TerminalSegment segment{TerminalTextSpan{""}}; // complete display + explicit zone label, atomic human LTR
};
// civilAt supplies explicit wall fields; ICU only decorates those Gregorian fields in a fixed GMT formatter.
// Named clocks require the independently configured civil-data owner, even for ASCII/backend recovery.
// ASCII uses unambiguous Gregorian YYYY-MM-DD / 24-hour time and an exact explicit offset/zone label.
// Failure preserves all prior output. No parsing, current-time read, automatic zone or calendar inference.
bool formatTerminalTime(const TerminalTimeRequest &,TerminalTime *);
} // namespace om
