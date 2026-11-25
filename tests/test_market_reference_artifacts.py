"""Default-off installed provider path, separate from financial and worker authority."""
from pathlib import Path
import unittest


class MarketReferenceArtifacts(unittest.TestCase):
    def test_parent_binds_only_the_explicit_installed_path(self):
        root = Path(__file__).resolve().parents[1]
        native = (root / 'src/core/miner.cu').read_text(encoding='utf-8')
        self.assertIn('if constexpr (kPublicTickerRuntimeBuilt)', native)
        self.assertIn('ordinaryRuntime && so.dashboard && !opt.plain && !opt.autotune, so.noMarket', native)
        self.assertIn('publicTickerPermissionArg = cli.restArgIndices[i];', native)
        self.assertIn('if (i == publicTickerPermissionArg) continue;', native)
        self.assertIn('publicHourPermissionArg = cli.restArgIndices[i];', native)
        self.assertIn('if (i == publicHourPermissionArg) continue;', native)
        self.assertIn('validDashboardHourConsent(argc, argv, publicTickerPermissionArg, publicHourPermissionArg)', native)
        self.assertEqual(native.count('installedPublicTickerFactory('), 1)
        self.assertNotIn('makeMarketReferenceRuntime', native)
        self.assertNotIn('publicFeedUrl(', native)
        factory = (root / 'src/product/market_ticker_runtime.cpp').read_text(encoding='utf-8')
        self.assertIn('module.parent_path() / "canary_public_json_worker', factory)
        self.assertIn('policy.scheduleEconomicRequests = false', factory)
        self.assertIn('policy.publicHour = enableHour', factory)
        for marker in ('getenv(', 'publicFeedUrl(', 'PublicJsonClient', '.get('):
            self.assertNotIn(marker, factory)
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
