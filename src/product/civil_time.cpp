#include "civil_time.h"
#include <algorithm>
#include <fstream>
#include <mutex>
#if defined(CANARY_NAMED_TZDB)
#include <date/tz.h>
#endif

namespace om {
namespace {
constexpr int64_t kDay = 86400000, kLast = 253402300799999LL;
bool leap(int year) { return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0); }
unsigned monthDays(int year, unsigned month) {
    static const unsigned n[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    return month >= 1 && month <= 12 ? n[month-1] + (month == 2 && leap(year) ? 1 : 0) : 0;
}
int64_t beforeYear(int year) { const int64_t y = year - 1; return y*365 + y/4 - y/100 + y/400; }
bool instant(int64_t ms) { return ms >= 0 && ms <= kLast; }
bool zoneName(const std::string &name) {
    if (name.empty() || name.size() > 128 || name.front() == '/' || name.back() == '/' || name.find("//") != std::string::npos) return false;
    for (char c : name) if (!(c >= 'a' && c <= 'z') && !(c >= 'A' && c <= 'Z') && !(c >= '0' && c <= '9') && c != '_' && c != '-' && c != '+' && c != '/') return false;
    return true;
}
bool validClock(const CivilClock &clock) {
    if (clock.kind == CivilClockKind::Utc) return clock.zone.empty() && clock.fixedOffsetSeconds == 0;
    if (clock.kind == CivilClockKind::FixedStandardOffset) return clock.zone.empty() && clock.fixedOffsetSeconds >= -86400 && clock.fixedOffsetSeconds <= 86400;
    return clock.kind == CivilClockKind::NamedIana && clock.fixedOffsetSeconds == 0 && zoneName(clock.zone);
}
bool versionName(const std::string &version) {
    if (version.size() < 5 || version.size() > 10) return false;
    for (size_t i = 0; i < version.size(); ++i)
        if (i < 4 ? version[i] < '0' || version[i] > '9' : version[i] < 'a' || version[i] > 'z') return false;
    return true;
}
struct Runtime {
    std::mutex mutex;
    bool attempted = false, ready = false;
    std::filesystem::path path;
    std::string version;
};
Runtime &runtime() { static Runtime value; return value; }
#if defined(CANARY_NAMED_TZDB)
const date::time_zone *zone(const CivilClock &clock) {
    auto &r = runtime(); std::lock_guard<std::mutex> guard(r.mutex);
    return r.ready ? date::locate_zone(clock.zone) : nullptr;
}
#endif
}  // namespace

bool sameCivilDate(CivilDate a, CivilDate b) { return a.year == b.year && a.month == b.month && a.day == b.day; }
bool validCivilDate(CivilDate date) { return date.year >= 1970 && date.year <= 9999 && date.day >= 1 && date.day <= monthDays(date.year,date.month); }
std::optional<int64_t> civilSerialDay(CivilDate date) {
    if (!validCivilDate(date)) return std::nullopt;
    int64_t days = beforeYear(date.year) - beforeYear(1970) + date.day - 1;
    for (unsigned m = 1; m < date.month; ++m) days += monthDays(date.year,m);
    return days;
}
std::optional<CivilDate> civilDateFromSerialDay(int64_t day) {
    if (day < 0 || day > kLast / kDay) return std::nullopt;
    int low = 1970, high = 10000;
    while (low + 1 < high) { const int mid = low + (high-low)/2; if (beforeYear(mid)-beforeYear(1970) <= day) low = mid; else high = mid; }
    unsigned remaining = static_cast<unsigned>(day - (beforeYear(low)-beforeYear(1970))), month = 1;
    while (remaining >= monthDays(low,month)) { remaining -= monthDays(low,month); ++month; }
    return CivilDate{low,month,remaining+1};
}
std::optional<CivilDate> civilAddDays(CivilDate date, int days) { const auto serial = civilSerialDay(date); return serial ? civilDateFromSerialDay(*serial + days) : std::nullopt; }
unsigned civilIsoWeekday(CivilDate date) { const auto n = civilSerialDay(date); return n ? static_cast<unsigned>((*n+3)%7)+1 : 0; }
const char *civilStatusName(CivilStatus status) {
    switch (status) {
        case CivilStatus::Ok: return "ok";
        case CivilStatus::Invalid: return "invalid/unsupported civil clock or instant";
        case CivilStatus::Unavailable: return "reviewed named time data unavailable";
        case CivilStatus::VersionMismatch: return "time database version mismatch";
        case CivilStatus::AlreadyConfigured: return "time data fixed for this process; restart required";
        case CivilStatus::DataInvalid: return "invalid/incomplete private time data package";
        case CivilStatus::Ambiguous: return "ambiguous local wall time needs explicit fold selection";
        case CivilStatus::Nonexistent: return "nonexistent local wall time";
        case CivilStatus::SkippedDate: return "local calendar date was skipped";
    }
    return "unsupported time status";
}
CivilStatus configureCivilTimeData(const std::filesystem::path &directory, const std::string &expected) {
    if (!directory.is_absolute() || !versionName(expected)) return CivilStatus::Invalid;
#if defined(CANARY_NAMED_TZDB)
    auto &r = runtime(); std::lock_guard<std::mutex> guard(r.mutex);
    if (r.attempted) return r.ready && directory == r.path && expected == r.version ? CivilStatus::Ok : CivilStatus::AlreadyConfigured;
    try {
        const char *files[] = {"africa","antarctica","asia","australasia","backward","etcetera","europe","northamerica","southamerica","leapseconds","version"
#if defined(_WIN32)
            ,"windowsZones.xml"
#endif
        };
        uint64_t total = 0;
        for (const auto *name : files) {
            const auto file = directory / name;
            if (!std::filesystem::is_regular_file(std::filesystem::symlink_status(file))) return CivilStatus::DataInvalid;
            const auto bytes = std::filesystem::file_size(file);
            if (!bytes || bytes > 2*1024*1024 || total + bytes > 16*1024*1024) return CivilStatus::DataInvalid;
            total += bytes;
        }
        if (std::filesystem::file_size(directory/"version") > 32) return CivilStatus::DataInvalid;
        std::ifstream version(directory/"version"); std::string found, extra;
        if (!(version >> found) || (version >> extra)) return CivilStatus::DataInvalid;
        if (found != expected) return CivilStatus::VersionMismatch;
        r.attempted = true; r.path = directory; r.version = expected;
        date::set_install(directory.u8string());
        if (date::get_tzdb().version != expected) return CivilStatus::VersionMismatch;
        if (!date::locate_zone("Etc/UTC")) return CivilStatus::DataInvalid;
        r.ready = true; return CivilStatus::Ok;
    } catch (...) { return CivilStatus::DataInvalid; }
#else
    return CivilStatus::Unavailable;
#endif
}
bool civilNamedTimeAvailable() { auto &r = runtime(); std::lock_guard<std::mutex> guard(r.mutex); return r.ready; }
CivilStamp civilAt(const CivilClock &clock, int64_t ms) {
    CivilStamp result;
    if (!validClock(clock) || !instant(ms)) { result.status = CivilStatus::Invalid; return result; }
    try {
        int64_t offset = clock.kind == CivilClockKind::FixedStandardOffset ? clock.fixedOffsetSeconds : 0;
        if (clock.kind == CivilClockKind::NamedIana) {
#if defined(CANARY_NAMED_TZDB)
            const auto *z = zone(clock); if (!z) return result;
            offset = z->get_info(date::sys_time<std::chrono::milliseconds>{std::chrono::milliseconds{ms}}).offset.count();
            result.databaseVersion = runtime().version;
#else
            return result;
#endif
        } else result.databaseVersion = "explicit-fixed-clock";
        if (offset < -86400 || offset > 86400 || !instant(ms + offset * 1000)) { result.status = CivilStatus::Invalid; return result; }
        const auto local = ms + offset * 1000;
        const auto date = civilDateFromSerialDay(local / kDay);
        if (!date) { result.status = CivilStatus::Invalid; return result; }
        result.date = *date; result.millisecondOfDay = static_cast<uint32_t>(local % kDay); result.isoWeekday = civilIsoWeekday(*date);
        result.offsetSeconds = static_cast<int32_t>(offset); result.status = CivilStatus::Ok; return result;
    } catch (...) { return result; }
}
CivilResolved civilResolve(const CivilClock &clock, CivilDate day, uint32_t localMs, CivilChoose choose) {
    CivilResolved result;
    const auto serial = civilSerialDay(day);
    if (!validClock(clock) || !serial || localMs >= kDay || (choose != CivilChoose::Reject && choose != CivilChoose::Earlier && choose != CivilChoose::Later)) { result.status = CivilStatus::Invalid; return result; }
    try {
        int64_t utc = *serial * kDay + localMs;
        if (clock.kind == CivilClockKind::NamedIana) {
#if defined(CANARY_NAMED_TZDB)
            const auto *z = zone(clock); if (!z) return result;
            const date::local_time<std::chrono::milliseconds> local{std::chrono::milliseconds{utc}};
            const auto info = z->get_info(local);
            if (info.result == date::local_info::ambiguous) result.kind = CivilWallKind::Ambiguous;
            if (info.result == date::local_info::nonexistent) result.kind = CivilWallKind::Nonexistent;
            if (choose == CivilChoose::Reject && result.kind != CivilWallKind::Unique) {
                result.status = result.kind == CivilWallKind::Ambiguous ? CivilStatus::Ambiguous : CivilStatus::Nonexistent; return result;
            }
            utc = z->to_sys(local,choose == CivilChoose::Later ? date::choose::latest : date::choose::earliest).time_since_epoch().count();
#else
            return result;
#endif
        } else if (clock.kind == CivilClockKind::FixedStandardOffset) utc -= static_cast<int64_t>(clock.fixedOffsetSeconds) * 1000;
        if (!instant(utc)) { result.status = CivilStatus::Invalid; return result; }
        result.utcMs = utc; result.status = CivilStatus::Ok; return result;
    } catch (...) { return result; }
}
CivilDay civilDay(const CivilClock &clock, CivilDate day) {
    CivilDay result; result.date = day;
    const auto next = civilAddDays(day,1);
    if (!next) { result.status = CivilStatus::Invalid; return result; }
    // Day boundaries explicitly choose first occurrence/first valid instant, not an implicit operator timestamp choice.
    const auto start = civilResolve(clock,day,0,CivilChoose::Earlier), end = civilResolve(clock,*next,0,CivilChoose::Earlier);
    if (start.status != CivilStatus::Ok) { result.status = start.status; return result; }
    if (end.status != CivilStatus::Ok) { result.status = end.status; return result; }
    if (*end.utcMs <= *start.utcMs) { result.status = CivilStatus::SkippedDate; return result; }
    result.beginUtcMs = *start.utcMs; result.endUtcMs = *end.utcMs;
    result.databaseVersion = civilAt(clock,result.beginUtcMs).databaseVersion;
    result.status = CivilStatus::Ok; return result;
}
CivilDay civilDayAt(const CivilClock &clock, int64_t ms) { const auto local = civilAt(clock,ms); if (local.status != CivilStatus::Ok) { CivilDay d; d.status = local.status; return d; } return civilDay(clock,local.date); }
std::optional<int64_t> civilNextTransition(const CivilClock &clock, int64_t ms) {
    if (!validClock(clock) || !instant(ms) || clock.kind != CivilClockKind::NamedIana) return std::nullopt;
#if defined(CANARY_NAMED_TZDB)
    try {
        const auto *z = zone(clock); if (!z) return std::nullopt;
        const auto end = z->get_info(date::sys_time<std::chrono::milliseconds>{std::chrono::milliseconds{ms}}).end.time_since_epoch().count();
        if (end >= 0 && end <= kLast/1000 && end*1000 > ms) return end*1000;
    } catch (...) {}
#endif
    return std::nullopt;
}
}  // namespace om
