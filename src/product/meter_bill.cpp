#include "meter_bill.h"
#include "local_preferences_store.h"
#include "tariff_declaration.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string_view>
#if defined(_WIN32)
#include <shellapi.h>
#endif

namespace om {
namespace {

constexpr size_t maxMeterFileBytes = 1024 * 1024;
constexpr size_t maxMeterIntervals = 8192;
constexpr int64_t maxUtcMs = 253402300799999LL;

struct MeterBillPaths { std::filesystem::path tariff, intervals; };

std::optional<MeterBillPaths> meterBillPaths(const MeterBillOptions &options, int argc, const char *const *argv) {
    if (options.tariffArg < 3 || options.tariffArg >= argc || options.intervalsArg < 3 || options.intervalsArg >= argc)
        return std::nullopt;
#if defined(_WIN32)
    (void)argv;
    // main's narrow argv can lose characters in filesystem operands on non-UTF-8 systems.
    struct WideArguments {
        LPWSTR *values = nullptr;
        ~WideArguments() { if (values) LocalFree(values); }
    } args;
    int count = 0;
    args.values = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!args.values || count != argc || std::wstring_view(args.values[1]) != L"meter-bill" ||
        std::wstring_view(args.values[options.tariffArg-1]) != L"--tariff" ||
        std::wstring_view(args.values[options.intervalsArg-1]) != L"--intervals")
        return std::nullopt;
    return MeterBillPaths{args.values[options.tariffArg], args.values[options.intervalsArg]};
#else
    return MeterBillPaths{argv[options.tariffArg], argv[options.intervalsArg]};
#endif
}

bool readInput(const std::filesystem::path &filename, size_t limit, std::string &out) {
    std::error_code ec;
    const auto path = std::filesystem::absolute(filename, ec);
    if (ec) return false;
#if defined(_WIN32)
    const auto root = path.root_name().native();
    if (root.size() != 2 || root[1] != L':' || path.native().find(L':', 2) != std::wstring::npos)
        return false;
    const auto drive = GetDriveTypeW(path.root_path().c_str());
    if (drive != DRIVE_FIXED && drive != DRIVE_REMOVABLE && drive != DRIVE_RAMDISK) return false;
    auto parent = path.root_path();
    const auto directories = path.parent_path().relative_path();
    for (const auto &component : directories) {
        parent /= component;
        const auto attributes = GetFileAttributesW(parent.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY) ||
            (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
    }
#endif
    return readBoundedLocalFile(path, limit, &out) == LocalFileRead::Ok;
}

bool instant(const JsonValue *value, int64_t &result) {
    uint64_t number = 0;
    if (!value || !value->asUint64(&number) || !number || number > maxUtcMs) return false;
    result = static_cast<int64_t>(number);
    return true;
}

const char *decodeIntervals(const std::string &text, const BillOpening &opening, int64_t now,
                            std::vector<PowerInterval> &readings) {
    JsonValue root;
    if (!parseJsonStrict(text, &root, nullptr) || !root.isObject() || root.members.size() != 3)
        return "invalid interval document";
    const auto *schema = root.find("schema_version");
    const auto *scope = root.find("scope");
    const auto *intervals = root.find("intervals");
    uint64_t version = 0;
    if (!schema || !schema->asUint64(&version) || version != 1 || !scope || !scope->isString() ||
        !intervals || !intervals->isArray() || intervals->items.empty() || intervals->items.size() > maxMeterIntervals)
        return "invalid interval document";
    if (scope->text != opening.evidence.scope) return "meter scope differs from the saved opening";

    auto cursor = opening.throughUtcMs;
    readings.reserve(intervals->items.size());
    for (const auto &row : intervals->items) {
        PowerInterval reading;
        const auto *watts = row.find("average_watts");
        if (!row.isObject() || row.members.size() != 3 ||
            !instant(row.find("begin_utc_ms"), reading.beginUtcMs) ||
            !instant(row.find("end_utc_ms"), reading.endUtcMs) || reading.endUtcMs <= reading.beginUtcMs ||
            !watts || !watts->isString() || !ExactDecimal::parse(watts->text, &reading.watts) ||
            reading.watts.text() != watts->text || reading.watts.negative() ||
            reading.watts.digits() > 38 || reading.watts.scale() > 18)
            return "invalid meter interval";
        if (reading.beginUtcMs != cursor) return "interval coverage has a gap or overlap";
        if (reading.endUtcMs > now) return "interval ends in the future";
        cursor = reading.endUtcMs;
        readings.push_back(std::move(reading));
    }
    return nullptr;
}

bool appendPeriod(std::string &report, unsigned period, const TariffDeclaration &declaration,
                  const BillOpening &opening, const BillAccrual &accrual) {
    ElectricityAmount usage;
    std::string usageText, change, total;
    if (!accrual.ending->importedKwh.numerator.subtract(opening.importedKwh.numerator, &usage.numerator) ||
        !usage.format(6, &usageText) || !accrual.provisionalChange->format(6, &change) ||
        !accrual.provisionalTotal->format(6, &total)) return false;

    const auto &currency = declaration.plan.currency;
    report += "Cycle " + std::to_string(period) + " [" + std::to_string(opening.cycleBeginUtcMs) + ", " +
              std::to_string(opening.cycleEndUtcMs) + ") UTC ms\n";
    report += "  Reviewed interval range [" + std::to_string(opening.throughUtcMs) + ", " +
              std::to_string(accrual.ending->throughUtcMs) + ") UTC ms\n";
    report += "  Imported usage " + usageText + " kWh\n";
    report += "  Change from imported readings " + change + " " + currency + "\n";
    report += accrual.settledTotal ? "  Cycle estimate at closing " : "  Provisional cycle estimate ";
    report += total + " " + currency;
    if (opening.throughUtcMs != opening.cycleBeginUtcMs) report += " (includes declared opening)";
    report += '\n';
    if (accrual.settlementDependent) report += "  Closing charges depend on full-cycle usage.\n";
    return true;
}

int refuse(const char *reason) {
    std::fprintf(stderr, "meter-bill: %s\n", reason);
    return 3;
}

}  // namespace

int commandMeterBill(const MeterBillOptions &options, int argc, const char *const *argv, const volatile sig_atomic_t *stop) {
    if (options.help) {
        std::puts("canary-miner meter-bill --tariff FILE --intervals FILE\n"
                  "Review a saved combined tariff against contiguous whole-meter intervals.\n"
                  "Uses average watts as constant import within each interval. Prints an estimate to stdout.");
        return 0;
    }
    const std::function<bool()> stopped = [stop] { return stop && *stop; };
    const auto fail = [&](const char *reason) { return stopped() ? 0 : refuse(reason); };
    try {
        const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        if (stop && *stop) return 0;
        const auto paths = meterBillPaths(options, argc, argv);
        if (!paths) return fail("could not resolve input file arguments");
        std::string tariffText, meterText;
        JsonValue root;
        TariffDeclaration declaration;
        if (stop && *stop) return 0;
        if (!readInput(paths->tariff, tariffDeclarationMaxBytes, tariffText) ||
            !parseJsonStrict(tariffText, &root, nullptr) || !decodeTariffDeclaration(root, &declaration))
            return fail("saved tariff declaration unavailable or invalid");
        if (now < 1 || now > maxUtcMs || static_cast<int64_t>(declaration.confirmedUtc * 1000) > now)
            return fail("local clock precedes the review or is unavailable");
        if (stop && *stop) return 0;
        if (!readInput(paths->intervals, maxMeterFileBytes, meterText))
            return fail("interval file unavailable, oversized or not a regular file");
        std::vector<PowerInterval> readings;
        if (const char *problem = decodeIntervals(meterText, declaration.opening, now, readings)) return fail(problem);

        std::string report = "Estimated electricity costs from a declared tariff and user-supplied meter intervals\n"
                             "Average import is applied uniformly within each interval.\n";
        auto opening = declaration.opening;
        size_t next = 0;
        auto cursor = opening.throughUtcMs;
        for (unsigned cycle = 1; next < readings.size(); ++cycle) {
            if (stop && *stop) return 0;
            if (cycle > maxTariffComparisonCycles) return fail("intervals span more than 12 cycles");
            PowerTrace slice{opening.evidence, {}};
            while (next < readings.size() && cursor < opening.cycleEndUtcMs) {
                const auto &reading = readings[next];
                const auto end = (std::min)(reading.endUtcMs, opening.cycleEndUtcMs);
                slice.intervals.push_back({cursor, end, reading.watts});
                cursor = end;
                if (cursor == reading.endUtcMs) ++next;
            }
            const auto accrual = accrueBill(declaration.plan, opening, slice, cursor, now, stopped);
            if (stop && *stop) return 0;
            if (accrual.status != ElectricityStatus::Ok) return fail(electricityStatusName(accrual.status));
            if (!appendPeriod(report, cycle, declaration, opening, accrual)) return fail("amount exceeds the display range");
            if (next < readings.size()) {
                opening = *accrual.ending;
                bool chargedDaySplit = false;
                if (advanceTariffOpening(declaration, opening, chargedDaySplit) != ElectricityStatus::Ok)
                    return fail(chargedDaySplit ? "a reset splits a tariff day with a daily fee" :
                                                    "next cycle unavailable under the saved reset rule");
            }
        }
        if (stop && *stop) return 0;
        if (std::fwrite(report.data(), 1, report.size(), stdout) != report.size() || std::fflush(stdout) != 0)
            return 1;
        return 0;
    } catch (...) {
        return fail("review could not be completed");
    }
}

}  // namespace om
