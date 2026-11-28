#include "octopus_rate_cli.h"

#include "local_review_archive.h"
#include "market_public_sample.h"
#include "market_ticker_runtime.h"
#include "octopus_json.h"
#include "octopus_meter_rates.h"

#include <cstdio>
#if defined(_WIN32)
#include <shellapi.h>
#endif

namespace om {
namespace {

constexpr auto captureBudget = std::chrono::seconds(120);
constexpr size_t pageBytes = 64 * 1024;

std::filesystem::path rateTariffPath(int argc, const char *const *argv) {
#if defined(_WIN32)
    (void)argv;
    int count = 0;
    struct Arguments {
        LPWSTR *values;
        ~Arguments() { if (values) LocalFree(values); }
    } args{CommandLineToArgvW(GetCommandLineW(), &count)};
    if (!args.values || count != argc || !args.values[4][0] || args.values[4][0] == L'-') return {};
    return args.values[4];
#else
    (void)argc;
    return argv[4];
#endif
}

bool pageRows(const std::string &body, unsigned page, unsigned limit, uint64_t &total) {
    JsonValue document;
    uint64_t count = 0;
    if (!parseJsonStrict(body, &document, nullptr, {pageBytes, 7}) ||
        !octopus_detail::fields(document, {"count", "next", "previous", "results"}) ||
        !document.find("count")->asUint64(&count) || !count || count > limit || (total && count != total)) return false;

    const auto preceding = (page - 1) * octopusRatePageSize;
    if (preceding >= count) return false;
    const auto &rows = *document.find("results");
    const auto expected = (std::min)(uint64_t{octopusRatePageSize}, count - preceding);
    if (!rows.isArray() || rows.items.size() != expected) return false;
    for (const auto &[field, present] : {std::pair{"next", preceding + expected < count}, std::pair{"previous", page > 1}}) {
        const auto &link = *document.find(field);
        if (present ? !link.isString() || link.text.empty() || link.text.size() > 2048 : link.type != JsonValue::Type::Null)
            return false;
    }
    total = count;
    return true;
}

int rateError(const char *what, int code = 3) {
    std::fprintf(stderr, "meter-rates: %s\n", what);
    return code;
}

}  // namespace

const char *capturePublicOctopusRates(TariffDeclaration declaration, const std::string &payment,
                                      const std::string &dns, const std::filesystem::path &module,
                                      const NativeProcessLifetime &lifetime, const volatile sig_atomic_t *stop,
                                      OctopusRateCapture &out, market_detail::MarketSampleClock *parentSample) {
    using namespace market_detail;
    const auto stopped = [&] { return marketSampleStopped(stop) || !lifetime.permitted(); };
    if (stopped()) return "rate capture cancelled";
    if (payment != "DIRECT_DEBIT" && payment != "NON_DIRECT_DEBIT") return "invalid Octopus payment method";
    if (const auto *problem = checkOctopusRateDeclaration(declaration)) return problem;
    const auto &plan = declaration.plan;
    const auto &revision = plan.revisions.front();
    const auto first = civilAt(plan.clock, revision.beginUtcMs);
    const auto last = civilAt(plan.clock, revision.endUtcMs);
    if (first.status != CivilStatus::Ok || first.millisecondOfDay || last.status != CivilStatus::Ok || last.millisecondOfDay)
        return "rate window must start and end at London local midnight";
    if (revision.beginUtcMs % octopus_detail::halfHourMs || revision.endUtcMs % octopus_detail::halfHourMs)
        return "rate window must use exact UTC half-hour boundaries";
    const auto started = marketTradeClock();
    if (parentSample && !marketTradeClockContinuous(*parentSample, *parentSample, started))
        return "local clock changed during the capture";
    if (!started.second || started.second > maxMarketUtcMs || revision.endUtcMs > static_cast<int64_t>(started.second) ||
        declaration.confirmedUtc > started.second / 1000) return "rate window and declaration must be wholly elapsed";

    const auto helper = installedPublicTickerHelper(module);
    if (helper.empty()) return "installed helper path unavailable";
    const auto fetch = marketProcessFetch(helper);
    PublicHttpsLimits limits;
    limits.bodyBytes = pageBytes;
    limits.dnsServer = dns;
    limits.parentDeadline = std::chrono::steady_clock::now() + captureBudget;
    PublicJsonRequest request{PublicFeed::OctopusUnitRates, revision.beginUtcMs / 1000, revision.endUtcMs / 1000,
                              {}, OctopusRatePage{plan.product, plan.territory.front(), 1}};
    std::string url;
    if (!publicFeedUrl(request, &url)) return "rate window exceeds the public request bounds";

    auto previous = started;
    const auto capture = [&](PublicFeed feed, unsigned rowLimit, std::string &pages) -> const char * {
        request.feed = feed;
        uint64_t count = 0;
        for (unsigned page = 1;; ++page) {
            request.octopus->page = page;
            const auto sent = marketTradeClock();
            if (!marketTradeClockContinuous(started, previous, sent)) return "local clock changed during the capture";
            const auto reply = awaitPublicMarketReply(fetch, request, limits, lifetime, stop);
            if (reply.status != PublicHttpsStatus::Ok) return publicHttpsStatusName(reply.status);
            const auto received = marketTradeClock();
            if (!marketTradeClockContinuous(started, sent, received) || reply.httpStatus != 200 ||
                reply.observedUtc < sent.second / 1000 || reply.observedUtc > received.second / 1000)
                return "invalid rate page observation time or HTTP status";
            if (!pageRows(reply.body, page, rowLimit, count)) return "invalid or incomplete public rate pages";
            if (reply.body.size() + pages.size() + 1 > maxOctopusRateFileBytes) return "public rate capture exceeds the size limit";
            if (page > 1) pages += ',';
            pages += reply.body;
            previous = received;
            if (page * octopusRatePageSize >= count) return nullptr;
        }
    };

    std::string unit, standing;
    if (const auto *problem = capture(PublicFeed::OctopusUnitRates, octopusMaxUnitRows, unit)) return problem;
    if (const auto *problem = capture(PublicFeed::OctopusStandingCharges, octopusMaxStandingRows, standing)) return problem;

    const auto observed = previous.second;
    const auto tariff = "E-1R-" + plan.product + "-" + plan.territory;
    auto bundle = "{\"schema_version\":1,\"product_code\":" + jsonQuote(plan.product) +
        ",\"tariff_code\":" + jsonQuote(tariff) + ",\"payment_method\":" + jsonQuote(payment) +
        ",\"observed_utc_ms\":" + std::to_string(observed) + ",\"unit_rate_pages\":[" + unit +
        "],\"standing_charge_pages\":[" + standing + "]}\n";
    if (bundle.size() > maxOctopusRateFileBytes) return "public rate capture exceeds the size limit";

    OctopusRateImport summary;
    if (const auto *problem = importOctopusRates(bundle, static_cast<int64_t>(observed), declaration, summary, stopped))
        return problem;
    if (validateTariffOpening(declaration.plan, declaration.opening, declaration.dailyFeeAllocation()) != ElectricityStatus::Ok)
        return "captured rates conflict with the tariff opening";
    if (stopped()) return "rate capture cancelled";
    out = {std::move(bundle), std::move(summary)};
    if (parentSample) *parentSample = previous;
    return nullptr;
}

int commandPublicOctopusRates(const OctopusRateOptions &options, const std::filesystem::path &module,
                              int argc, const char *const *argv, const volatile sig_atomic_t *stop) {
    using namespace market_detail;
    NativeProcessLifetime lifetime;
    const auto stopped = [&] { return marketSampleStopped(stop) || !lifetime.permitted(); };
    const auto fail = [&](const char *message) {
        if (!lifetime.permitted()) return rateError("helper ownership uncertain");
        if (marketSampleStopped(stop)) return rateError("rate capture cancelled", 130);
        return rateError(message);
    };
    if (marketSampleStopped(stop)) return rateError("rate capture cancelled", 130);
    if (!lifetime.begin()) return rateError("helper ownership uncertain");
    try {
        const auto target = localReviewPath(options.output);
        std::error_code fileError;
        if (!target || std::filesystem::symlink_status(*target, fileError).type() != std::filesystem::file_type::not_found)
            return fail("rate output path is unavailable or already exists");
        std::string bytes;
        JsonValue root;
        TariffDeclaration declaration;
        const auto path = rateTariffPath(argc, argv);
        if (path.empty() || !readLocalReviewInput(path, tariffDeclarationMaxBytes, bytes) ||
            !parseJsonStrict(bytes, &root, nullptr)) return fail("saved tariff declaration unavailable or invalid");
        if (configureBundledCivilTimeData() != CivilStatus::Ok)
            return fail("reviewed tariff time database unavailable or invalid");
        if (!decodeTariffDeclaration(root, &declaration))
            return fail("saved tariff declaration unavailable or invalid");
        OctopusRateCapture captured;
        if (const auto *problem = capturePublicOctopusRates(std::move(declaration), options.payment, options.dns,
                                                           module, lifetime, stop, captured)) return fail(problem);
        using local_review_archive::ReviewSave;
        const auto save = local_review_archive::saveWhileActive(options.output, ".json", captured.bytes,
                                                               maxOctopusRateFileBytes, stopped);
        if (save == ReviewSave::Stopped) return fail("rate capture stopped before publication");
        if (save == ReviewSave::Uncertain)
            return rateError("publication or cleanup is uncertain; inspect the rate file and its temporary file");
        if (save != ReviewSave::Written) return fail("could not create a new rate file; any existing file was preserved");

        const auto &summary = captured.summary;
        return std::printf("Saved %zu Octopus %s rate intervals at UTC ms %lld\n", summary.intervals,
                           summary.go ? "Go" : "Agile", static_cast<long long>(summary.observedUtcMs)) < 0 ||
               std::fflush(stdout) != 0 ? 1 : 0;
    } catch (...) {
        return fail("could not complete the public rate capture");
    }
}

}  // namespace om
