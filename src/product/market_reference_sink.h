// Default-empty native presentation seam. No credentials, source selection, mining or economic authority.
#pragma once
#include "market_view.h"
#include <cstdint>
#include <functional>
#include <memory>
namespace om {
class MarketReferenceSink {
   public:
    virtual ~MarketReferenceSink()=default;
    virtual bool start(uint64_t generation)=0; // one explicit parent; bounded memory/thread admission only
    virtual PublicMarketView view(uint64_t nowUtc,uint64_t nowMonoMs)const=0; // read-only projection, never I/O
    virtual void requestStop()=0; // memory-only revocation; never join here
    virtual void stop()=0; // actual background/helper settlement after worker drain
};
using MarketReferenceFactory=std::function<std::unique_ptr<MarketReferenceSink>()>;
} // namespace om
