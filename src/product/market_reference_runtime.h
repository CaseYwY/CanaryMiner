// Explicit frozen provider owner adapter, not default native activation or a supplier/terms qualification.
#pragma once
#include "market_reference_sink.h"
#include "market_owner.h"
namespace om {
std::unique_ptr<MarketReferenceSink> makeMarketReferenceRuntime(MarketFetch,MarketPollingPolicy={},
    MarketOrigin=MarketOrigin::ProviderResponse,bool fixtureCandles=false,MarketClock={});
} // namespace om
