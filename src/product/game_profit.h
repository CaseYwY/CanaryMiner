// Exact synthetic game-mode economics window. Not an actual-income adapter or game-performance qualification.
#pragma once
#include "income_forecast.h"
#include <atomic>
namespace om {
constexpr uint64_t gameProfitWindowMs=600000; // ten real elapsed minutes, including donation/pause/transition time
enum class GameProfitStatus { Ok, Unsupported, Invalid, Attribution, Coverage, Clock, Capacity, Cancelled };
struct GameProfitBinding {
    std::string gameKey,profileKey,episodeKey;
    CreditSourceContract production;
    PowerEvidence power;
};
bool sameGameProfitBinding(const GameProfitBinding &a,const GameProfitBinding &b);
struct GameProfitInterval {
    uint64_t beginMonoMs=0,endMonoMs=0;
    std::string modelReceipt; // fixture namespace; future real adapters must supply qualified source/calculation proof
    std::optional<ForecastRatio> expectedRevenueUsd,estimatedCostUsd; // AMOUNTS for this entire interval, not unweighted rates
};
struct GameProfitInput {
    ForecastQualification qualification=ForecastQualification::Unsupported;
    GameProfitBinding binding;
    uint64_t generation=0,revision=0,beginMonoMs=0,endMonoMs=0;
    int64_t beginUtcMs=0,endUtcMs=0;
    std::vector<GameProfitInterval> intervals;
};
class GameProfitWindow {
   public:
    const GameProfitInput &input()const{return input_;}
    const ForecastRatio &revenueUsd()const{return revenue_;}
    const ForecastRatio &costUsd()const{return cost_;}
    const ForecastRatio &netUsd()const{return net_;}
    const ForecastRatio &meanNetUsdPerSecond()const{return mean_;}
    bool positive()const{return !net_.numerator.negative()&&!net_.numerator.zero();}
   private:
    GameProfitInput input_;ForecastRatio revenue_,cost_,net_,mean_;
    GameProfitWindow(GameProfitInput input,ForecastRatio revenue,ForecastRatio cost,ForecastRatio net,ForecastRatio mean)
        :input_(std::move(input)),revenue_(std::move(revenue)),cost_(std::move(cost)),net_(std::move(net)),mean_(std::move(mean)){}
    friend std::shared_ptr<const GameProfitWindow> buildGameProfitWindow(const GameProfitInput &,GameProfitStatus *,const std::atomic<bool> *);
};
std::shared_ptr<const GameProfitWindow> buildGameProfitWindow(const GameProfitInput &input,GameProfitStatus *status=nullptr,const std::atomic<bool> *cancel=nullptr);
} // namespace om
