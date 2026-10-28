// Explicit UTC/fixed-standard/named-IANA clocks. No system-zone inference, locale coupling or data download.
#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace om {
struct CivilDate { int year = 1970; unsigned month = 1, day = 1; };
bool sameCivilDate(CivilDate a, CivilDate b);
bool validCivilDate(CivilDate date);
std::optional<int64_t> civilSerialDay(CivilDate date);
std::optional<CivilDate> civilDateFromSerialDay(int64_t day);
std::optional<CivilDate> civilAddDays(CivilDate date, int days);
unsigned civilIsoWeekday(CivilDate date);  // 1 Monday .. 7 Sunday; 0 invalid

enum class CivilClockKind { Utc, FixedStandardOffset, NamedIana };
struct CivilClock {
    CivilClockKind kind = CivilClockKind::Utc;
    std::string zone;
    int32_t fixedOffsetSeconds = 0;
};
enum class CivilStatus { Ok, Invalid, Unavailable, VersionMismatch, AlreadyConfigured, DataInvalid, Ambiguous, Nonexistent, SkippedDate };
const char *civilStatusName(CivilStatus status);
enum class CivilChoose { Reject, Earlier, Later };
enum class CivilWallKind { Unique, Ambiguous, Nonexistent };
struct CivilStamp {
    CivilStatus status = CivilStatus::Unavailable;
    CivilDate date;
    uint32_t millisecondOfDay = 0;
    unsigned isoWeekday = 0;
    int32_t offsetSeconds = 0;
    std::string databaseVersion;
};
struct CivilResolved {
    CivilStatus status = CivilStatus::Unavailable;
    CivilWallKind kind = CivilWallKind::Unique;
    std::optional<int64_t> utcMs;
};
struct CivilDay {
    CivilStatus status = CivilStatus::Unavailable;
    CivilDate date;
    int64_t beginUtcMs = 0, endUtcMs = 0;  // [begin,end), first valid local midnight boundary
    std::string databaseVersion;
};
// Explicit process-lifetime configuration of reviewed app-private text TZDB. No hot reload or fallback to OS data.
CivilStatus configureCivilTimeData(const std::filesystem::path &absoluteDirectory, const std::string &exactVersion);
bool civilNamedTimeAvailable();
CivilStamp civilAt(const CivilClock &clock, int64_t utcMs);
CivilResolved civilResolve(const CivilClock &clock, CivilDate date, uint32_t millisecondOfDay, CivilChoose choose = CivilChoose::Reject);
CivilDay civilDay(const CivilClock &clock, CivilDate date);
CivilDay civilDayAt(const CivilClock &clock, int64_t utcMs);
std::optional<int64_t> civilNextTransition(const CivilClock &clock, int64_t utcMs);
}  // namespace om
