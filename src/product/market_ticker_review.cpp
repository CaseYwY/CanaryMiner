#include "market_ticker_review.h"
#include "json_dom.h"
#include "local_review_input.h"
#include "market_data.h"

#include <chrono>
#include <cstdio>
#if defined(_WIN32)
#include <shellapi.h>
#endif

namespace om {
namespace {

constexpr size_t maxTickerFileBytes=64*1024;
constexpr uint64_t maxUtc=253402300799ULL;

std::optional<std::filesystem::path> reviewFile(int argc,const char *const *argv) {
    if (argc!=4) return {};
#if defined(_WIN32)
    (void)argv;
    int count=0;
    struct NativeArguments {
        LPWSTR *values=nullptr;
        ~NativeArguments() { if (values) LocalFree(values); }
    } arguments{CommandLineToArgvW(GetCommandLineW(),&count)};
    const auto *a=arguments.values;
    if (!a || count!=argc || std::wstring_view(a[1])!=L"market-tickers" ||
        std::wstring_view(a[2])!=L"--file" || !a[3][0] || a[3][0]==L'-') return {};
    return std::filesystem::path(a[3]);
#else
    if (std::string_view(argv[1])!="market-tickers" || std::string_view(argv[2])!="--file" ||
        !argv[3][0] || argv[3][0]=='-') return {};
    return std::filesystem::path(argv[3]);
#endif
}

bool readQuote(const JsonValue *record,PublicFeed feed,uint64_t cutoff,MarketDataResult<MarketQuote> &quote) {
    if (!record) return true;
    const auto *at=record->find("observed_utc"), *body=record->find("response_json");
    uint64_t observation=0;
    if (!record->isObject() || record->members.size()!=2 || !at || !at->asUint64(&observation) ||
        !observation || observation>cutoff || !body || !body->isString() || body->text.empty())
        return false;
    MarketParseContext context;
    context.request.feed=feed;
    context.receivedUtc=cutoff;
    context.generation=1;
    context.origin=MarketOrigin::DeclaredPayload;
    quote=parseMarketQuote({PublicHttpsStatus::Ok,200,body->text,observation},context);
    return true;
}

void appendQuote(std::string &report,const char *label,const JsonValue *record,
                 const MarketDataResult<MarketQuote> &quote,const std::optional<ExactDecimal> &fresh,
                 const std::optional<uint64_t> &age) {
    report+=std::string(label)+": ";
    if (!record) report+="N/A (source not supplied)";
    else if (!quote.value) report+="N/A ("+std::string(marketDataStatusName(quote.status))+")";
    else {
        report+=fresh ? fresh->text() : "N/A (outside declared age limit)";
        report+=" | declared observation UTC "+std::to_string(quote.value->observation.observedUtc);
        if (age) report+=" | age at cutoff "+std::to_string(*age/1000)+" s";
    }
    report+='\n';
}

int reportError(const char *message,int code=3) {
    std::fprintf(stderr,"market-tickers: %s\n",message);
    return code;
}

}  // namespace

int commandTickerFile(int argc,const char *const *argv,const volatile sig_atomic_t *stop) {
    const auto stopped=[&] { return stop && *stop; };
    const auto cancelled=[] { return reportError("review cancelled",130); };
    if (stopped()) return cancelled();
    try {
        const auto path=reviewFile(argc,argv);
        if (!path) return reportError("invalid arguments; use market-tickers --help",2);
        std::string text;
        const auto read=readLocalReviewInput(*path,maxTickerFileBytes,text);
        if (stopped()) return cancelled();
        if (!read) return reportError("input file unavailable, oversized or unsuitable");

        JsonValue root;
        if (!parseJsonStrict(text,&root,nullptr,{maxTickerFileBytes,4}) || !root.isObject())
            return reportError("invalid ticker review document");
        const auto *version=root.find("schema_version"), *asOf=root.find("as_of_utc");
        const auto *gate=root.find("gate"), *kraken=root.find("kraken");
        uint64_t schema=0, cutoff=0;
        if (!version || !version->asUint64(&schema) || schema!=1 || !asOf || !asOf->asUint64(&cutoff) ||
            !cutoff || cutoff>maxUtc || (!gate && !kraken) ||
            root.members.size()!=2u+static_cast<unsigned>(gate!=nullptr)+static_cast<unsigned>(kraken!=nullptr))
            return reportError("invalid ticker review document");
        const auto now=std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        if (now<1 || static_cast<uint64_t>(now)>maxUtc || cutoff>static_cast<uint64_t>(now))
            return reportError("declared review cutoff is in the future or local UTC is unavailable");
        MarketDataResult<MarketQuote> pearl,fx;
        if (!readQuote(gate,PublicFeed::GatePearlTicker,cutoff,pearl))
            return reportError("invalid Gate observation record");
        if (!readQuote(kraken,PublicFeed::KrakenUsdtTicker,cutoff,fx))
            return reportError("invalid Kraken observation record");
        const auto latest=declaredMarketReference(pearl.value ? &*pearl.value : nullptr,
                                                   fx.value ? &*fx.value : nullptr,cutoff);
        std::string report="Historical ticker review from declared payloads (offline)\n";
        report+="Declared cutoff UTC "+std::to_string(cutoff)+" | no trade-event timestamps or current freshness claim\n";
        report+="Displayed prices use the declared providers' last-trade values.\n";
        appendQuote(report,"Declared Gate Pearl mainnet PRL/USDT",gate,pearl,latest.tokenUsdt,latest.tokenObservationAgeMs);
        appendQuote(report,"Declared Kraken USDT/USD",kraken,fx,latest.usdtUsd,latest.fxObservationAgeMs);
        report+="Declared PRL/USD reference at cutoff: ";
        if (latest.tokenUsd) report+=latest.tokenUsd->text();
        else if (latest.usdStatus==MarketFreshness::Skew)
            report+="N/A (observations more than "+std::to_string(declaredTickerV1Policy.maxCrossFeedSkewMs/1000)+" s apart)";
        else
            report+="N/A (requires both valid observations within "+
                std::to_string(declaredTickerV1Policy.maxObservationAgeMs/1000)+" s of cutoff)";
        report+='\n';
        if (stopped()) return cancelled();
        if (std::fwrite(report.data(),1,report.size(),stdout)!=report.size() || std::fflush(stdout)!=0) return 1;
        return latest.tokenUsd ? 0 : 3;
    } catch (...) {
        return stopped() ? cancelled() : reportError("could not review ticker file");
    }
}

}  // namespace om
