"""Default-empty provider runtime seam, not source/terms, live data or production qualification."""
from pathlib import Path
import unittest


class MarketReferenceArtifacts(unittest.TestCase):
    def test_ordinary_entry_does_not_select_a_public_source(self):
        root = Path(__file__).resolve().parents[1]
        native = (root / 'src/core/miner.cu').read_text(encoding='utf-8')
        self.assertNotIn('marketReferenceFactory', native)
        self.assertNotIn('makeMarketReferenceRuntime', native)
        sink = (root / 'src/product/market_reference_sink.h').read_text(encoding='utf-8')
        for marker in ('public_https.h', 'market_owner.h', 'filesystem', 'wallet', 'PublicJsonRequest'):
            self.assertNotIn(marker, sink)
        stats = (root / 'src/product/stats.h').read_text(encoding='utf-8')
        self.assertNotIn('MarketReference', stats)
        self.assertNotIn('publicMarket', stats)

    def test_revocation_stays_separate_from_join_and_transport_selection(self):
        root = Path(__file__).resolve().parents[1]
        source = (root / 'src/product/market_owner.cpp').read_text(encoding='utf-8')
        revoke = source.split('void MarketDataOwner::requestStop() {', 1)[1].split('void MarketDataOwner::stop()', 1)[0]
        self.assertIn('impl_->cancel = true', revoke)
        self.assertNotIn('.join()', revoke)
        self.assertNotIn('fetch(', revoke)
        runtime = (root / 'src/product/market_reference_runtime.cpp').read_text(encoding='utf-8')
        for marker in ('marketProcessFetch(', 'PublicJsonProcessClient', 'std::filesystem', 'publicFeedUrl(', 'getenv('):
            self.assertNotIn(marker, runtime)
        # Actual source admission remains explicitly unavailable; native coupling cannot relabel fixtures.
        header = (root / 'src/product/market_data.h').read_text(encoding='utf-8')
        self.assertIn('enum class MarketMinuteConvention { Unqualified, FixtureMinuteStart };', header)


if __name__ == '__main__':
    unittest.main()
