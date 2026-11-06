#include "market_reference_runtime.h"
namespace om {
namespace {
// Lifecycle and view calls belong to one parent control thread, as do the owning supervisor's other sinks.
class Runtime final:public MarketReferenceSink {
    MarketPollingPolicy policy_;MarketDataOwner owner_;
    bool attempted_=false,started_=false,stopped_=false;
   public:
    Runtime(MarketFetch fetch,MarketPollingPolicy policy,MarketOrigin origin,bool fixture,MarketClock clock)
        :policy_(policy),owner_(std::move(fetch),policy,origin,fixture,std::move(clock)){}
    ~Runtime()override{stop();}
    bool start(uint64_t generation)override{
        if(attempted_)return false;
        attempted_=true;if(stopped_||!generation)return false;
        started_=owner_.start(generation);return started_;
    }
    PublicMarketView view(uint64_t utc,uint64_t mono)const override{
        if(!started_)return {};
        auto snapshot=*owner_.snapshot();if(stopped_)snapshot.running=false;
        return marketDashboardView(snapshot,policy_,utc,mono);
    }
    void requestStop()override{attempted_=stopped_=true;owner_.requestStop();}
    void stop()override{requestStop();owner_.stop();}
};
}
std::unique_ptr<MarketReferenceSink> makeMarketReferenceRuntime(MarketFetch fetch,MarketPollingPolicy policy,MarketOrigin origin,bool fixture,MarketClock clock){
    return std::make_unique<Runtime>(std::move(fetch),policy,origin,fixture,std::move(clock));
}
} // namespace om
