#include "energy_periods_cli.h"

#include "energy_runtime.h"
#include "local_review_input.h"
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
    std::filesystem::path stateDir, policy;
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
    return EnergyReportPaths{args.values[options.stateDirArg], args.values[options.policyArg]};
#else
    (void)argc;
    return EnergyReportPaths{argv[options.stateDirArg], argv[options.policyArg]};
#endif
}

std::vector<TariffCycleBounds> reportPeriods(const JsonValue &policy, int64_t through, unsigned count) {
    std::vector<TariffCycleBounds> cycles;
    for (unsigned i = 0; i < count; ++i) {
        JsonValue at;
        at.type = JsonValue::Type::Number;
        at.text = std::to_string(through);
        TariffCycleBounds period;
        if (!deriveTariffMonthlyCycle(policy, at, period) || period.beginUtcMs <= 0 ||
            (!cycles.empty() && period.endUtcMs != cycles.back().beginUtcMs))
            return {};
        cycles.push_back(period);
        through = period.beginUtcMs - 1;
    }
    std::reverse(cycles.begin(), cycles.end());
    return cycles;
}

int fail(const char *message, int code = 3) {
    std::fprintf(stderr, "energy-periods: %s\n", message);
    return code;
}

std::optional<std::string> renderReport(const std::vector<TariffCycleBounds> &cycles, const EnergyHistoryReply &reply,
                                        int64_t through, size_t channels) {
    std::string output = "Local GPU-board energy history (NVML sample-and-hold)\n"
                         "UTC milliseconds, exclusive period end. Coverage is measured in GPU-ms.\n";
    output += "Audited sequence " + std::to_string(reply.checkpoint.sequence) + ", latest committed batch UTC ms " +
              (reply.checkpoint.sequence ? std::to_string(reply.checkpoint.observedAtMs) : std::string("unavailable")) +
              ", selected boards " + std::to_string(channels) + '\n';
    auto totals = reply.totals.begin();
    for (const auto &cycle : cycles) {
        const auto end = (std::min)(through, cycle.endUtcMs);
        output += "Period [" + std::to_string(cycle.beginUtcMs) + ", " + std::to_string(cycle.endUtcMs) +
                  ") through " + std::to_string(end) + ": ";
        if (end == cycle.beginUtcMs) {
            output += "no elapsed time\n";
            continue;
        }
        if (totals == reply.totals.end()) return std::nullopt;
        const auto &total = *totals++;
        if (total.coverage == EnergyHistoryCoverage::Unavailable) output += "unavailable";
        else if (total.coverage == EnergyHistoryCoverage::Partial) output += "partial";
        else output += "complete";
        output += " (" + std::to_string(total.coveredDeviceMs) + "/" + std::to_string(total.requestedDeviceMs) + " GPU-ms)";
        if (total.coveredDeviceMs) {
            std::string kwh;
            if (!total.knownKwh.format(9, &kwh)) return std::nullopt;
            output += ", covered energy " + kwh + " kWh (exact " + total.knownKwh.numerator.text() + " W-ms)";
        }
        output += '\n';
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
        const auto paths = reportPaths(options, argc, argv);
        if (!paths) return fail("invalid arguments; use energy-periods --help", 2);
        std::string raw;
        JsonValue policy;
        if (!readLocalReviewInput(paths->policy, 4096, raw) || !boundedPublicJson(raw) ||
            !parseJsonStrict(raw, &policy, nullptr) || !policy.isObject())
            return fail("monthly reset policy unavailable or invalid");

        const auto utcNow = [] {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
        };
        const auto now = utcNow();
        const int64_t through = options.throughUtcMs ? options.throughUtcMs : now;
        if (through <= 0 || through > now) return fail("cutoff must be in the past or present");
        configureBundledCivilTimeData();
        const auto cycles = reportPeriods(policy, through, options.cycles);
        if (cycles.empty()) return fail("monthly reset periods unavailable for the selected clock");

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
        for (const auto &cycle : cycles) {
            const auto end = (std::min)(through, cycle.endUtcMs);
            if (end != cycle.beginUtcMs) request.queries.push_back({cycle.beginUtcMs, end, {}, {}});
        }

        const auto queried = queryEnergyPeriods(runtime->collectorHelper, request, cancel, timeoutMs);
        if (cancel) return stopped();
        if (queried.transport == LocalTaskStatus::Busy)
            return fail("energy collection is active or its lease is unavailable");
        if (!queried.reply || queried.reply->opened != EnergyHistoryStatus::Ok)
            return fail(queried.reply && queried.reply->opened == EnergyHistoryStatus::Conflict ?
                        "stored observations require recovery review" : "verified local energy history unavailable");
        const auto report = renderReport(cycles, *queried.reply, through, request.binding.channels.size());
        if (cancel) return stopped();
        if (!report) return fail("energy report unavailable");
        if (std::fwrite(report->data(), 1, report->size(), stdout) != report->size() || std::fflush(stdout) != 0)
            return fail("report could not be written");
        return 0;
    } catch (...) {
        return cancel ? stopped() : fail("energy report unavailable");
    }
}

} // namespace om
