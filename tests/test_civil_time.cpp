#include "product/civil_time.h"
#include "product_test_util.h"
#include <iostream>
using namespace om;
namespace {
constexpr int64_t hour = 3600000;
int64_t utc(CivilDate date, uint32_t ms = 0) {
    const auto r = civilResolve({},date,ms);
    CHECK(r.status == CivilStatus::Ok && r.utcMs);
    return r.utcMs.value_or(0);
}
void dayLength(const CivilClock &z, CivilDate date, int64_t length) {
    const auto d = civilDay(z,date);
    CHECK(d.status == CivilStatus::Ok && d.endUtcMs-d.beginUtcMs == length);
    CHECK(sameCivilDate(civilAt(z,d.beginUtcMs).date,date));
    CHECK(sameCivilDate(civilAt(z,d.endUtcMs-1).date,date));
}
}
int main(int argc, char **argv) {
    CHECK(!civilNamedTimeAvailable());
    CHECK(civilAt({CivilClockKind::NamedIana,"America/New_York",0},utc({2026,1,1})).status == CivilStatus::Unavailable);
    CHECK(configureCivilTimeData("relative","2026e") == CivilStatus::Invalid);
    CHECK(configureCivilTimeData(std::filesystem::current_path(),"latest") == CivilStatus::Invalid);
    CHECK(!validCivilDate({1900,1,1}) && !validCivilDate({10000,1,1}));
    CHECK(!validCivilDate({2026,0,1}) && !validCivilDate({2026,13,1}) && !validCivilDate({2026,2,29}));
    CHECK(validCivilDate({2000,2,29}) && !validCivilDate({2100,2,29}));
    CHECK(!civilSerialDay({2026,1,0}) && !civilDateFromSerialDay(-1));
    CHECK(!civilDateFromSerialDay(2932897));
    CHECK(!civilAddDays({1970,1,1},-1) && !civilAddDays({9999,12,31},1));
    for (int64_t serial = 0; serial < 157420; ++serial) {
        const auto date = civilDateFromSerialDay(serial);
        CHECK(date && civilSerialDay(*date) == serial);
        if (serial % 397 == 0) {
            const auto s = civilAt({},serial*86400000+12345);
            CHECK(s.status == CivilStatus::Ok && sameCivilDate(s.date,*date) && s.millisecondOfDay == 12345);
        }
    }
    CHECK(civilIsoWeekday({1970,1,1}) == 4 && civilIsoWeekday({2026,10,28}) == 3);
    CHECK(civilAt({},-1).status == CivilStatus::Invalid);
    CHECK(civilAt({},253402300800000LL).status == CivilStatus::Invalid);
    CHECK(civilAt({},253402300799999LL).date.year == 9999);
    CHECK(civilResolve({}, {2026,1,1},86400000).status == CivilStatus::Invalid);
    CHECK(civilAt({CivilClockKind::Utc,"UTC",0},0).status == CivilStatus::Invalid);
    CHECK(civilAt({CivilClockKind::FixedStandardOffset,"",86401},0).status == CivilStatus::Invalid);
    CHECK(civilAt({CivilClockKind::NamedIana,"../private",0},0).status == CivilStatus::Invalid);
    CHECK(civilAt({CivilClockKind::NamedIana,"America//New_York",0},0).status == CivilStatus::Invalid);
    CivilClock fixed{CivilClockKind::FixedStandardOffset,"",10*3600};
    const auto s = civilAt(fixed,utc({2026,1,1},15*hour));
    CHECK(s.status == CivilStatus::Ok && sameCivilDate(s.date,{2026,1,2}) && s.millisecondOfDay == hour);
    CHECK(civilResolve(fixed,s.date,s.millisecondOfDay).utcMs == utc({2026,1,1},15*hour));
    dayLength({}, {2026,3,8},24*hour);
    dayLength(fixed,{2026,3,8},24*hour);
    CHECK(!civilNextTransition(fixed,utc({2026,1,1})));
    if (argc == 2) {
        const auto directory = std::filesystem::u8path(argv[1]);
        CHECK(configureCivilTimeData(directory/"missing","2026e") == CivilStatus::DataInvalid);
        CHECK(configureCivilTimeData(directory,"2026a") == CivilStatus::VersionMismatch);
        CHECK(configureCivilTimeData(directory,"2026e") == CivilStatus::Ok);
        CHECK(civilNamedTimeAvailable());
        CHECK(configureCivilTimeData(directory,"2026e") == CivilStatus::Ok);
        CHECK(configureCivilTimeData(directory,"2026a") == CivilStatus::AlreadyConfigured);
        CivilClock ny{CivilClockKind::NamedIana,"America/New_York",0};
        dayLength(ny,{2026,3,8},23*hour);
        dayLength(ny,{2026,11,1},25*hour);
        const auto missing = civilResolve(ny,{2026,3,8},2*hour+30*60000);
        CHECK(missing.status == CivilStatus::Nonexistent && !missing.utcMs);
        const auto clamp = civilResolve(ny,{2026,3,8},2*hour+30*60000,CivilChoose::Earlier);
        CHECK(clamp.status == CivilStatus::Ok && clamp.kind == CivilWallKind::Nonexistent && clamp.utcMs == utc({2026,3,8},7*hour));
        const auto amb = civilResolve(ny,{2026,11,1},hour+30*60000);
        CHECK(amb.status == CivilStatus::Ambiguous && !amb.utcMs);
        const auto early = civilResolve(ny,{2026,11,1},hour+30*60000,CivilChoose::Earlier);
        const auto late = civilResolve(ny,{2026,11,1},hour+30*60000,CivilChoose::Later);
        CHECK(early.status == CivilStatus::Ok && late.status == CivilStatus::Ok && *late.utcMs-*early.utcMs == hour);
        CHECK(civilAt(ny,*early.utcMs).offsetSeconds != civilAt(ny,*late.utcMs).offsetSeconds);
        CHECK(civilAt(ny,*early.utcMs).databaseVersion == "2026e");
        CHECK(civilNextTransition(ny,utc({2026,3,8})) == utc({2026,3,8},7*hour));
        CHECK(civilNextTransition(ny,utc({2026,3,8},7*hour)) == utc({2026,11,1},6*hour));
        dayLength({CivilClockKind::NamedIana,"Australia/Lord_Howe",0},{2026,4,5},24*hour+30*60000);
        dayLength({CivilClockKind::NamedIana,"Australia/Lord_Howe",0},{2026,10,4},23*hour+30*60000);
        CHECK(civilDay({CivilClockKind::NamedIana,"Pacific/Apia",0},{2011,12,30}).status == CivilStatus::SkippedDate);
        dayLength({CivilClockKind::NamedIana,"America/Winnipeg",0},{2026,11,1},24*hour);
        CHECK(civilAt({CivilClockKind::NamedIana,"Etc/Not_A_Zone",0},utc({2026,1,1})).status != CivilStatus::Ok);
        std::cout << "Reviewed development fixture TZDB: 2026e (not a signed production package)\n";
    }
    return finishTests("civil time");
}
