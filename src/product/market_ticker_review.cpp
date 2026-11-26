#include "market_ticker_review.h"
#include "hash.h"
#include "json_dom.h"
#include "local_review_archive.h"
#include "market_data.h"
#include "market_public_sample.h"

#include <chrono>
#include <cstdio>
#if defined(_WIN32)
#include <shellapi.h>
#endif

namespace om {
namespace {

constexpr size_t maxTickerFileBytes=64*1024;
constexpr uint64_t maxUtc=253402300799ULL;
constexpr size_t tickerBodyBytes=1024*1024, tickerReportBytes=8192;
constexpr size_t tickerArchiveBytes=2*tickerBodyBytes+tickerReportBytes+1024;
constexpr uint64_t maxCaptureMs=120000;
constexpr MarketFreshnessPolicy tickerCaptureV1Policy{120000,30000,false};
constexpr std::string_view tickerArchiveHeader="CANARY-MARKET-TICKER-REVIEW-1\n";
constexpr std::string_view tradeArchiveHeader="CANARY-MARKET-TRADE-REVIEW-1\n";

std::optional<std::filesystem::path> reviewFile(MarketTickerAction action, int argc, const char *const *argv) {
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
        std::wstring_view(a[2])!=(action==MarketTickerAction::OpenReview?L"--open-review":L"--file") ||
        !a[3][0] || a[3][0]==L'-') return {};
    return std::filesystem::path(a[3]);
#else
    if (std::string_view(argv[1])!="market-tickers" ||
        std::string_view(argv[2])!=(action==MarketTickerAction::OpenReview?"--open-review":"--file") ||
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

bool renderTickerCapture(const PublicTickerCapture &capture, std::string &report) {
    using market_detail::marketSampleClockContinuous;
    const auto &gate=capture.gate, &kraken=capture.kraken;
    const market_detail::MarketSampleClock started{0,capture.startedUtc}, first{gate.elapsedMs,gate.receivedUtc},
        second{kraken.elapsedMs,kraken.receivedUtc}, cutoff{capture.elapsedMs,capture.cutoffUtc};
    if (!capture.startedUtc || capture.elapsedMs>maxCaptureMs ||
        gate.sentUtc<capture.startedUtc || kraken.sentUtc<gate.receivedUtc ||
        !marketSampleClockContinuous(started,started,first) ||
        !marketSampleClockContinuous(started,first,second) ||
        !marketSampleClockContinuous(started,second,cutoff))
        return false;
    const auto parse=[&](const PublicTickerReceipt &receipt, PublicFeed feed) {
        const MarketParseContext context{{feed},receipt.sentUtc,receipt.elapsedMs,1,MarketOrigin::ProviderResponse,
                                         MarketMinuteConvention::Unqualified,receipt.receivedUtc};
        return parseMarketQuote(receipt.response,context).value;
    };
    const auto pearl=parse(gate,PublicFeed::GatePearlTicker), fx=parse(kraken,PublicFeed::KrakenUsdtTicker);
    const auto latest=latestMarketReference(pearl?&*pearl:nullptr,fx?&*fx:nullptr,
                                            capture.cutoffUtc,capture.elapsedMs,tickerCaptureV1Policy);
    if (!latest.tokenUsd) return false;
    report="Historical public ticker capture (ages at capture)\n"
           "Capture UTC "+std::to_string(capture.cutoffUtc)+" | no trade-event timestamps or current freshness claim\n";
    const auto append=[&](const char *label,const MarketQuote &quote,const PublicTickerReceipt &receipt) {
        report+=std::string(label)+": "+quote.last.text()+" | observed UTC "+
            std::to_string(quote.observation.observedUtc)+" | age at capture "+
            std::to_string(capture.cutoffUtc-quote.observation.observedUtc)+" s\n";
        report+="  Request start UTC "+std::to_string(receipt.sentUtc)+" | receipt UTC "+
            std::to_string(receipt.receivedUtc)+'\n';
    };
    append("Gate Pearl mainnet PRL/USDT",*pearl,gate);
    append("Kraken USDT/USD",*fx,kraken);
    report+="PRL/USD reference at capture: "+latest.tokenUsd->text()+'\n';
    return true;
}

bool renderCapture(const PublicTickerCapture &capture,std::string &report) {
    return capture.trades ? renderPublicTradeSample(capture,report)==PublicTradeReport::Complete :
                            renderTickerCapture(capture,report);
}

void encodeTickerReceipt(std::string &bytes,const PublicTickerReceipt &receipt) {
    bytes+=std::to_string(receipt.sentUtc)+'\n'+std::to_string(receipt.response.observedUtc)+'\n'+
        std::to_string(receipt.receivedUtc)+'\n'+std::to_string(receipt.elapsedMs+1)+'\n'+
        std::to_string(receipt.response.body.size())+'\n'+receipt.response.body;
}

bool decodeTickerReceipt(const std::string &bytes,size_t &offset,PublicTickerReceipt &receipt,bool trades) {
    using local_review_archive::readArchiveNumber;
    int64_t sent=0, observed=0, received=0, elapsed=0;
    std::string body;
    const auto clockLimit=trades ? market_detail::maxMarketUtcMs : maxUtc;
    if (!readArchiveNumber(bytes,offset,clockLimit,sent) || !readArchiveNumber(bytes,offset,maxUtc,observed) ||
        !readArchiveNumber(bytes,offset,clockLimit,received) || !readArchiveNumber(bytes,offset,maxCaptureMs+1,elapsed) ||
        !local_review_archive::readArchivePart(bytes,offset,trades?maxPublicTradeBodyBytes:tickerBodyBytes,body)) return false;
    receipt={static_cast<uint64_t>(sent),static_cast<uint64_t>(received),static_cast<uint64_t>(elapsed-1),
             {PublicHttpsStatus::Ok,200,std::move(body),static_cast<uint64_t>(observed)}};
    return true;
}

const char *replayTickerCapture(const std::string &bytes,uint64_t nowMs,std::string &report) {
    using local_review_archive::readArchiveNumber;
    const bool trades=std::string_view(bytes).substr(0,tradeArchiveHeader.size())==tradeArchiveHeader;
    const auto header=trades ? tradeArchiveHeader : tickerArchiveHeader;
    auto offset=header.size();
    const auto clockLimit=trades ? market_detail::maxMarketUtcMs : maxUtc;
    int64_t started=0, cutoff=0, elapsed=0;
    PublicTickerCapture capture;
    capture.trades=trades;
    std::string savedReport;
    if (std::string_view(bytes).substr(0,offset)!=header ||
        !readArchiveNumber(bytes,offset,clockLimit,started) || !readArchiveNumber(bytes,offset,clockLimit,cutoff) ||
        !readArchiveNumber(bytes,offset,maxCaptureMs+1,elapsed) ||
        !decodeTickerReceipt(bytes,offset,capture.gate,trades) || !decodeTickerReceipt(bytes,offset,capture.kraken,trades) ||
        !local_review_archive::readArchivePart(bytes,offset,tickerReportBytes,savedReport) ||
        bytes.size()-offset!=65 || bytes.back()!='\n' ||
        bytes.compare(offset,64,sha256Hex(bytes.substr(0,offset)))!=0)
        return "ticker review archive unavailable or invalid";
    capture.startedUtc=static_cast<uint64_t>(started);
    capture.cutoffUtc=static_cast<uint64_t>(cutoff);
    capture.elapsedMs=static_cast<uint64_t>(elapsed-1);
    const auto now=trades ? nowMs : nowMs/1000;
    if (!now || now>clockLimit || capture.cutoffUtc>now || !renderCapture(capture,report))
        return trades ? "invalid times or incomplete fresh trade pair at capture" :
                        "invalid times or incomplete fresh ticker pair at capture";
    return report==savedReport?nullptr:"saved ticker report differs from the recalculated report";
}

int writeTickerReport(const std::string &report) {
    return std::fwrite(report.data(),1,report.size(),stdout)==report.size() && std::fflush(stdout)==0 ? 0 : 1;
}

}  // namespace

int savePublicTickerReview(const std::string &name, const PublicTickerCapture &capture,
                           const NativeProcessLifetime &lifetime, const volatile sig_atomic_t *stop) {
    using local_review_archive::ReviewSave;
    const auto stopped=[&] { return market_detail::marketSampleStopped(stop) || !lifetime.permitted(); };
    const auto interrupted=[&] {
        return lifetime.permitted()?reportError("sample cancelled",130):reportError("helper ownership uncertain");
    };
    if (stopped()) return interrupted();
    std::string report;
    const auto bodyLimit=capture.trades ? maxPublicTradeBodyBytes : tickerBodyBytes;
    const bool valid=capture.gate.response.body.size()<=bodyLimit && capture.kraken.response.body.size()<=bodyLimit &&
        renderCapture(capture,report) && report.size()<=tickerReportBytes;
    if (stopped()) return interrupted();
    if (!valid) return reportError(capture.trades ? "could not save trade review: both valid, fresh, aligned trades required" :
        "could not save ticker review: both valid, fresh, aligned observations required");
    std::string bytes(capture.trades?tradeArchiveHeader:tickerArchiveHeader);
    bytes+=std::to_string(capture.startedUtc)+'\n'+std::to_string(capture.cutoffUtc)+'\n'+
        std::to_string(capture.elapsedMs+1)+'\n';
    encodeTickerReceipt(bytes,capture.gate);
    encodeTickerReceipt(bytes,capture.kraken);
    bytes+=std::to_string(report.size())+'\n'+report;
    const auto state=local_review_archive::saveWhileActive(name,tickerReviewExtension,
        bytes+sha256Hex(bytes)+'\n',tickerArchiveBytes,stopped);
    if (state==ReviewSave::Stopped) return interrupted();
    if (state==ReviewSave::Uncertain)
        return reportError("archive publication or cleanup is uncertain; inspect the review and its temporary file");
    if (state!=ReviewSave::Written) return reportError("could not create the new ticker archive; any existing archive was preserved");
    return writeTickerReport(report);
}

int commandTickerFile(MarketTickerAction action,int argc,const char *const *argv,const volatile sig_atomic_t *stop) {
    const auto stopped=[&] { return stop && *stop; };
    const auto cancelled=[] { return reportError("review cancelled",130); };
    if (stopped()) return cancelled();
    try {
        const auto path=reviewFile(action,argc,argv);
        if (!path) return reportError("invalid arguments; use market-tickers --help",2);
        std::string text;
        const bool open=action==MarketTickerAction::OpenReview;
        const auto read=readLocalReviewInput(*path,open?tickerArchiveBytes:maxTickerFileBytes,text);
        if (stopped()) return cancelled();
        if (!read) return reportError("input file unavailable, oversized or unsuitable");
        const auto nowMs=market_detail::marketUtcClockMs();
        if (open) {
            std::string report;
            const auto *problem=replayTickerCapture(text,nowMs,report);
            if (stopped()) return cancelled();
            if (problem) return reportError(problem);
            return writeTickerReport(report);
        }

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
        const auto now=nowMs/1000;
        if (!now || now>maxUtc || cutoff>now)
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
        if (const auto write=writeTickerReport(report)) return write;
        return latest.tokenUsd ? 0 : 3;
    } catch (...) {
        return stopped() ? cancelled() : reportError("could not review ticker file");
    }
}

}  // namespace om
