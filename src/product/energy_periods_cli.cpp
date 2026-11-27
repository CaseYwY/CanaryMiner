#include "energy_periods_cli.h"

#include "electricity_preferences.h"
#include "energy_runtime.h"
#include "local_review_input.h"
#include "native_process_admission.h"
#include "octopus_meter_rates.h"
#include "public_https.h" // bounded input JSON only
#include "tariff_declaration.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#if defined(_WIN32)
#include <shellapi.h>
#endif

namespace om {
namespace {

struct EnergyReportPaths {
    std::filesystem::path stateDir, policy, tariff, octopusRates;
    std::vector<std::filesystem::path> flatRates;
};

struct ReportRate {
    int64_t beginUtcMs, endUtcMs, confirmedUtcMs;
    ExactDecimal ratePerKwh;
};

struct ReportPricing {
    std::string currency;
    std::vector<ReportRate> rates;
    const char *label = "Declared flat";
    int64_t capturedUtcMs = 0;
};

struct ReportSlice {
    int64_t beginUtcMs, endUtcMs;
    const ReportRate *rate;
};

struct ReportPeriod {
    TariffCycleBounds cycle;
    std::vector<ReportSlice> slices;
};

std::optional<EnergyReportPaths> reportPaths(const EnergyPeriodsOptions &options,
                                             int argc, const char *const *argv) {
#if defined(_WIN32)
    (void)argv;
    struct WideArguments {
        LPWSTR *values;
        ~WideArguments() { if (values) LocalFree(values); }
    };
    int count = 0;
    const WideArguments args{CommandLineToArgvW(GetCommandLineW(), &count)};
    if (!args.values || count != argc || std::wstring_view(args.values[1]) != L"energy-periods" ||
        std::wstring_view(args.values[options.stateDirArg - 1]) != L"--state-dir" ||
        std::wstring_view(args.values[options.policyArg - 1]) != L"--reset-policy" ||
        (options.cyclesArg && (std::wstring_view(args.values[options.cyclesArg - 1]) != L"--cycles" ||
                                std::wstring_view(args.values[options.cyclesArg]) != std::to_wstring(options.cycles))) ||
        (options.throughArg && (std::wstring_view(args.values[options.throughArg - 1]) != L"--through-utc-ms" ||
                                 std::wstring_view(args.values[options.throughArg]) != std::to_wstring(options.throughUtcMs))))
        return std::nullopt;
    const auto path = [&args](int index) { return std::filesystem::path(args.values[index]); };
    const auto matches = [&args](int index, const wchar_t *flag) {
        return !index || (std::wstring_view(args.values[index - 1]) == flag &&
                          args.values[index][0] && args.values[index][0] != L'-');
    };
    if (!matches(options.tariffArg, L"--tariff") || !matches(options.octopusRatesArg, L"--octopus-rates"))
        return std::nullopt;
    for (int index : options.flatRateArgs)
        if (!matches(index, L"--flat-rate")) return std::nullopt;
#else
    (void)argc;
    const auto path = [argv](int index) { return std::filesystem::path(argv[index]); };
#endif
    EnergyReportPaths result{path(options.stateDirArg), path(options.policyArg), {}, {}, {}};
    if (options.tariffArg) {
        result.tariff = path(options.tariffArg);
        result.octopusRates = path(options.octopusRatesArg);
    }
    for (int index : options.flatRateArgs) result.flatRates.push_back(path(index));
    return result;
}

const char *loadFlatPricing(const std::vector<std::filesystem::path> &files, int64_t now,
                             const std::atomic<bool> &cancel, ReportPricing &pricing) {
    for (const auto &file : files) {
        if (cancel) return "report stopped";
        std::string bytes;
        ElectricityPreferences value;
        if (!readLocalReviewInput(file, 8192, bytes) || !boundedPublicJson(bytes) ||
            !parseElectricityPreferences(bytes, &value, nullptr) || !value.tariffEnabled)
            return "flat rate declaration unavailable, disabled or invalid";
        if (pricing.currency.empty()) pricing.currency = value.currency;
        if (pricing.currency != value.currency) return "flat rate declarations must use one currency";
        ReportRate rate{static_cast<int64_t>(value.effectiveFromUtc * 1000),
                        static_cast<int64_t>(value.effectiveThroughUtc * 1000),
                        static_cast<int64_t>(value.confirmedUtc * 1000), {}};
        if (rate.confirmedUtcMs > now) return "flat rate confirmation is in the future";
        if (!ExactDecimal::parse(value.ratePerKwh, &rate.ratePerKwh))
            return "flat rate declaration unavailable, disabled or invalid";
        pricing.rates.push_back(std::move(rate));
    }
    std::sort(pricing.rates.begin(), pricing.rates.end(), [](const auto &left, const auto &right) {
        return left.beginUtcMs < right.beginUtcMs;
    });
    if (std::adjacent_find(pricing.rates.begin(), pricing.rates.end(), [](const auto &left, const auto &right) {
            return left.endUtcMs > right.beginUtcMs;
        }) != pricing.rates.end()) return "flat rate effective windows overlap";
    return nullptr;
}

const char *loadOctopusPricing(const EnergyReportPaths &paths, int64_t through, CivilStatus clockStatus,
                               const std::atomic<bool> &cancel, ReportPricing &pricing) {
    if (clockStatus != CivilStatus::Ok) return "reviewed tariff time database unavailable or invalid";
    std::string bytes;
    JsonValue root;
    TariffDeclaration declaration;
    if (!readLocalReviewInput(paths.tariff, tariffDeclarationMaxBytes, bytes) ||
        !parseJsonStrict(bytes, &root, nullptr) || !decodeTariffDeclaration(root, &declaration))
        return "saved tariff declaration unavailable or invalid";
    if (declaration.confirmedUtc > static_cast<uint64_t>(through / 1000))
        return "tariff declaration confirmed after the report cutoff";
    if (cancel) return "report stopped";
    if (!readLocalReviewInput(paths.octopusRates, maxOctopusRateFileBytes, bytes))
        return "saved Octopus rate capture unavailable";
    OctopusRateImport imported;
    if (const auto *problem = importOctopusRates(bytes, through, declaration, imported, [&] { return cancel.load(); }))
        return problem;
    pricing.currency = declaration.plan.currency;
    pricing.capturedUtcMs = imported.observedUtcMs;
    pricing.label = imported.go ? "Captured Octopus Go" : "Captured Octopus Agile";
    pricing.rates.reserve(imported.intervals);
    for (auto &revision : declaration.plan.revisions) {
        for (auto &rate : revision.dynamic) {
            if (!pricing.rates.empty()) {
                auto &previous = pricing.rates.back();
                if (previous.endUtcMs == rate.beginUtcMs && previous.confirmedUtcMs == rate.publishedUtcMs &&
                    previous.ratePerKwh.compare(rate.ratePerKwh) == 0) {
                    // A standing-fee split or a repeated unit price does not change the priced rate.
                    previous.endUtcMs = rate.endUtcMs;
                    continue;
                }
            }
            pricing.rates.push_back({rate.beginUtcMs, rate.endUtcMs, rate.publishedUtcMs, std::move(rate.ratePerKwh)});
        }
    }
    return nullptr;
}

std::vector<TariffCycleBounds> reportPeriods(const JsonValue &policy, int64_t through, unsigned count) {
    std::vector<TariffCycleBounds> cycles;
    for (unsigned i = 0; i < count; ++i) {
        JsonValue at;
        at.type = JsonValue::Type::Number;
        at.text = std::to_string(through);
        TariffCycleBounds period;
        if (!deriveTariffRecurringCycle(policy, at, period) || period.beginUtcMs <= 0 ||
            (!cycles.empty() && period.endUtcMs != cycles.back().beginUtcMs))
            return {};
        cycles.push_back(period);
        through = period.beginUtcMs - 1;
    }
    std::reverse(cycles.begin(), cycles.end());
    return cycles;
}

std::vector<ReportPeriod> splitPeriods(const std::vector<TariffCycleBounds> &cycles, int64_t through,
                                       const std::vector<ReportRate> &rates, std::vector<EnergyHistoryQuery> &queries) {
    std::vector<ReportPeriod> periods;
    for (const auto &cycle : cycles) {
        auto &period = periods.emplace_back(ReportPeriod{cycle, {}});
        const auto end = (std::min)(through, cycle.endUtcMs);
        auto cursor = cycle.beginUtcMs;
        const auto append = [&](int64_t finish, const ReportRate *rate) {
            if (cursor >= finish) return;
            period.slices.push_back({cursor, finish, rate});
            queries.push_back({cursor, finish, {}, {}});
            cursor = finish;
        };
        for (const auto &rate : rates) {
            const auto eligibleBegin = rate.beginUtcMs + energyHistoryClockGuardMs;
            const auto eligibleEnd = rate.endUtcMs - energyHistoryClockGuardMs;
            if (eligibleBegin >= end) break;
            if (rate.confirmedUtcMs > through || eligibleBegin >= eligibleEnd || eligibleEnd <= cursor) continue;
            append(eligibleBegin, nullptr);
            append((std::min)(eligibleEnd, end), &rate);
        }
        append(end, nullptr);
    }
    return periods;
}

int fail(const char *message, int code = 3) {
    std::fprintf(stderr, "energy-periods: %s\n", message);
    return code;
}

std::optional<std::string> renderReport(const std::vector<ReportPeriod> &periods, const EnergyHistoryReply &reply,
                                        int64_t through, size_t channels, const ReportPricing &pricing) {
    std::string output = "Local GPU-board energy history (NVML sample-and-hold)\n"
                         "UTC milliseconds, exclusive period end. Coverage is measured in GPU-ms.\n";
    output += "Audited sequence " + std::to_string(reply.checkpoint.sequence) + ", latest committed batch UTC ms " +
              (reply.checkpoint.sequence ? std::to_string(reply.checkpoint.observedAtMs) : std::string("unavailable")) +
              ", selected boards " + std::to_string(channels) + '\n';
    if (!pricing.currency.empty())
        output += std::string(pricing.label) + " GPU-board energy-only estimates (" + pricing.currency + ")\n";
    if (pricing.capturedUtcMs)
        output += "Price capture observed UTC ms " + std::to_string(pricing.capturedUtcMs) + '\n';
    auto totals = reply.totals.begin();
    for (const auto &period : periods) {
        const auto &cycle = period.cycle;
        const auto end = (std::min)(through, cycle.endUtcMs);
        output += "Period [" + std::to_string(cycle.beginUtcMs) + ", " + std::to_string(cycle.endUtcMs) +
                  ") through " + std::to_string(end) + ": ";
        if (end == cycle.beginUtcMs) {
            output += "no elapsed time\n";
            continue;
        }
        uint64_t requested = 0, covered = 0, priced = 0;
        ElectricityAmount energy, cost;
        for (const auto &slice : period.slices) {
            if (totals == reply.totals.end() || totals->beginUtcMs != slice.beginUtcMs || totals->endUtcMs != slice.endUtcMs)
                return std::nullopt;
            const auto &total = *totals++;
            requested += total.requestedDeviceMs;
            covered += total.coveredDeviceMs;
            if (!energy.numerator.add(total.knownKwh.numerator, &energy.numerator)) return std::nullopt;
            if (slice.rate && total.coveredDeviceMs) {
                ExactDecimal amount;
                if (!total.knownKwh.numerator.multiply(slice.rate->ratePerKwh, &amount) ||
                    !cost.numerator.add(amount, &cost.numerator)) return std::nullopt;
                priced += total.coveredDeviceMs;
            }
        }
        if (!requested) return std::nullopt;
        output += !covered ? "unavailable" : covered < requested ? "partial" : "complete";
        output += " (" + std::to_string(covered) + "/" + std::to_string(requested) + " GPU-ms)";
        if (covered) {
            std::string kwh;
            if (!energy.format(9, &kwh)) return std::nullopt;
            output += ", covered energy " + kwh + " kWh (exact " + energy.numerator.text() + " W-ms)";
        }
        output += '\n';
        if (!pricing.currency.empty()) {
            output += "  Priced coverage " + std::to_string(priced) + "/" + std::to_string(requested) + " GPU-ms, estimate ";
            if (priced) {
                std::string amount;
                if (!cost.format(9, &amount)) return std::nullopt;
                output += amount + " " + pricing.currency + " (exact " + cost.numerator.text() + "/" +
                          std::to_string(wattMsPerKwh) + " " + pricing.currency + ")";
            } else output += "unavailable";
            output += '\n';
        }
    }
    if (totals != reply.totals.end()) return std::nullopt;
    return output;
}

} // namespace

int commandEnergyPeriods(const EnergyPeriodsOptions &options, int argc, const char *const *argv,
                         const std::atomic<bool> &cancel) {
    const auto stopped = [] { return fail("stopped", 130); };
    try {
        if (cancel) return stopped();
        NativeProcessLifetime lifetime;
        if (!lifetime.begin()) return fail("energy helper ownership uncertain");
        const NativeProcessScope scope(lifetime.epoch());
        const auto paths = reportPaths(options, argc, argv);
        if (!paths) return fail("invalid arguments; use energy-periods --help", 2);
        std::string raw;
        JsonValue policy;
        if (!readLocalReviewInput(paths->policy, 4096, raw) || !boundedPublicJson(raw) ||
            !parseJsonStrict(raw, &policy, nullptr) || !policy.isObject())
            return fail("reset policy unavailable or invalid");

        const auto utcNow = [] {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
        };
        const auto now = utcNow();
        const int64_t through = options.throughUtcMs ? options.throughUtcMs : now;
        if (through <= 0 || through > now) return fail("cutoff must be in the past or present");
        const auto clockStatus = configureBundledCivilTimeData();
        const auto cycles = reportPeriods(policy, through, options.cycles);
        if (cycles.empty()) return fail(policy.find("monthly_day") ?
            "monthly reset periods unavailable for the selected clock" : "reset periods unavailable for the selected clock");
        ReportPricing pricing;
        const bool octopus = !paths->octopusRates.empty();
        const auto *problem = octopus ? loadOctopusPricing(*paths, through, clockStatus, cancel, pricing) :
            loadFlatPricing(paths->flatRates, now, cancel, pricing);
        if (problem) return cancel ? stopped() : fail(problem);
        std::vector<EnergyHistoryQuery> ranges;
        const auto periods = splitPeriods(cycles, through, pricing.rates, ranges);
        if (ranges.size() > (octopus ? maxEnergyDynamicPeriodQueries : maxEnergyPeriodQueries))
            return fail("report exceeds the price and reset range limit");
        if (cancel) return stopped();
        if (!lifetime.permitted()) return fail("energy helper ownership uncertain");

        std::error_code pathError;
        const auto directory = std::filesystem::absolute(paths->stateDir, pathError);
        const auto runtime = !pathError ? energyRuntimePaths(energyRuntimeExecutable(), directory) : std::nullopt;
        if (!runtime) return fail("local energy history unavailable");

        constexpr unsigned timeoutMs = 10000;
        EnergyAuthorizationRequest authorization;
        authorization.generation = authorization.ticket = 1;
        authorization.file = runtime->authorizationFile;
        authorization.nowUtcMs = utcNow();
        const auto loaded = EnergyAuthorizationProcessClient(runtime->authorizationHelper).transact(authorization, cancel, timeoutMs);
        if (cancel) return stopped();
        if (!lifetime.permitted()) return fail("energy helper ownership uncertain");
        if (!loaded.reply || loaded.reply->status != EnergyAuthorizationStatus::Ok ||
            !loaded.reply->document || loaded.reply->document->enrollment != EnergyEnrollment::Ready)
            return fail("enrolled local energy history unavailable");
        const auto &document = *loaded.reply->document;
        EnergyHistoryRequest request;
        request.generation = request.ticket = 1;
        request.recovery = EnergyRecoveryMode::Inspect;
        request.file = energyAuthorizationStorePath(authorization.file, document);
        request.binding = document.binding;
        request.limits = document.limits;
        request.nowUtcMs = authorization.nowUtcMs;
        request.queries = std::move(ranges);

        const auto queried = queryEnergyPeriods(runtime->collectorHelper, request, cancel, timeoutMs);
        if (cancel) return stopped();
        if (!lifetime.permitted()) return fail("energy helper ownership uncertain");
        if (queried.transport == LocalTaskStatus::Busy)
            return fail("energy collection is active or its lease is unavailable");
        if (!queried.reply || queried.reply->opened != EnergyHistoryStatus::Ok)
            return fail(queried.reply && queried.reply->opened == EnergyHistoryStatus::Conflict ?
                        "stored observations require recovery review" : "verified local energy history unavailable");
        const auto report = renderReport(periods, *queried.reply, through, request.binding.channels.size(), pricing);
        if (cancel) return stopped();
        if (!lifetime.permitted()) return fail("energy helper ownership uncertain");
        if (!report) return fail("energy report unavailable");
        if (std::fwrite(report->data(), 1, report->size(), stdout) != report->size() || std::fflush(stdout) != 0)
            return fail("report could not be written");
        return 0;
    } catch (...) {
        return cancel ? stopped() : fail("energy report unavailable");
    }
}

} // namespace om
