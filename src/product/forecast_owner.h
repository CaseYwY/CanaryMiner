// Optional background read/compute owner. No importer, ledger mutation, model authority from tickets or mining control.
#pragma once
#include "income_forecast.h"
#include "forecast_query_process.h"
#include <functional>
namespace om {
struct ForecastTime {int64_t utcMs=0;uint64_t monotonicMs=0;};
using ForecastOwnerClock=std::function<ForecastTime()>; // pure, nonblocking; production default uses both system/steady clocks
struct ForecastOwnerPolicy {
    bool enabled=false;
    bool suppressMarket=false,suppressEnergyCosts=false; // frozen session overrides; token facts remain independent
    unsigned projectionRefreshMs=1000,helperTimeoutMs=2000,readBudgetMs=10000;
    CreditLedgerLimits ledger;
};
struct ForecastWorkPeriod {int64_t beginUtcMs=0,endUtcMs=0;ForecastWork work;};
struct ForecastScenario {
    CivilClock clock;
    int64_t reportUtcMs=0,modelValidUntilUtcMs=0;
    ForecastQualification qualification=ForecastQualification::Unsupported;
    std::string modelKey,profileKey,earningAlignmentContract;
    std::vector<ForecastWorkPeriod> calibration;
    ForecastSchedule remaining;
    std::optional<MarketQuote> tokenQuote,fxQuote;
    MarketFreshnessPolicy marketPolicy;
    ForecastElectricity electricity;
};
enum class ForecastOwnerState { Stopped, Waiting, Busy, Ready, ClockFault, Faulted };
enum class ForecastAdmission { Queued, NotReady, Stopped, StaleGeneration, Invalid };
struct ForecastOwnerSnapshot {
    ForecastOwnerState state=ForecastOwnerState::Stopped;
    ForecastStatus status=ForecastStatus::Unavailable;
    uint64_t generation=0,revision=0,lastTicket=0,completedTicket=0,helperReads=0;
    bool pending=false;
    LocalTaskStatus transport=LocalTaskStatus::Unavailable;
    std::optional<CreditCursorCheckpoint> source; // private committed identity, not a queue or recovery target
    std::shared_ptr<const IncomeForecast> result;
    FinancialView view;
};
struct ForecastFxUpdate {bool replace=false;std::optional<ForecastCurrencyFx> value;};
class ForecastSink {
   public:
    virtual ~ForecastSink()=default;
    virtual bool start(uint64_t generation)=0;
    virtual void requestStop()=0;
    virtual void stop()=0;
    virtual std::shared_ptr<const ForecastOwnerSnapshot> snapshot()const=0;
};
using ForecastSinkFactory=std::function<std::unique_ptr<ForecastSink>(bool suppressMarket,bool suppressEnergyCosts)>;
class IncomeForecastOwner final:public ForecastSink {
   public:
    IncomeForecastOwner(std::filesystem::path installedQueryHelper,std::filesystem::path existingCreditFile,
                        CreditSourceContract binding,ForecastOwnerPolicy policy={},ForecastOwnerClock clock={});
    ~IncomeForecastOwner()override;
    IncomeForecastOwner(const IncomeForecastOwner &)=delete;
    IncomeForecastOwner &operator=(const IncomeForecastOwner &)=delete;
    bool start(uint64_t generation)override; // once; starts no read until an explicit bounded scenario is admitted
    ForecastAdmission submit(uint64_t generation,const ForecastScenario &scenario);
    ForecastAdmission updateMarket(uint64_t generation,std::optional<MarketQuote> token,std::optional<MarketQuote> fx,
                                   ForecastFxUpdate tariffFx={}); // absent update preserves independently refreshed tariff FX
    bool invalidate(uint64_t generation); // external source/profile change: cancel/discard, no automatic new read
    void requestStop()override; // memory-only revocation, including after stop-before-start
    void stop()override;        // join outside miner/safety/render paths
    std::shared_ptr<const ForecastOwnerSnapshot> snapshot()const override;
   private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace om
