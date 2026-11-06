"""Data-owner lifetime guards are cooperative memory scopes, not shipping test hooks or activation."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class DataOwnerNativeArtifacts(unittest.TestCase):
    def test_all_six_owners_bind_and_transfer_original_lifetimes(self):
        for unit in ('billing_owner', 'billing_compute_owner', 'forecast_owner', 'market_owner', 'game_detection_owner', 'safety_telemetry_owner'):
            with self.subTest(unit=unit):
                source = (ROOT / ('src/product/' + unit + '.cpp')).read_text(encoding='utf8')
                for marker in ('NativeProcessLifetime', 'NativeProcessScope original', 'nativeCurrent()', 'nativeLife.begin()', 'nativeLife.permitted()'):
                    self.assertIn(marker, source)
                for marker in ('__wrap_', 'RTLD_NEXT', 'OM_TEST_DATA_NATIVE_LIFETIME', 'canaryNativeAllocationFault'):
                    self.assertNotIn(marker, source)

    def test_market_snapshot_does_not_mutate_background_plan(self):
        source = (ROOT / 'src/product/market_owner.cpp').read_text(encoding='utf8')
        getter = source.split('MarketDataOwner::snapshot() const {', 1)[1].split('std::vector<std::string> marketSnapshotLines', 1)[0]
        self.assertNotIn('plan.', getter)
        for marker in ('maskedSource', 'emptyNativeFault', 'foreignScope', 'nativeFaulted'):
            self.assertIn(marker, getter)

    def test_self_helper_uses_no_storage_native_scan_or_sensor_adapter(self):
        source = (ROOT / 'tests/test_data_owner_native_lifetime.cpp').read_text(encoding='utf8')
        self.assertIn('argc>=2&&std::string(argv[1])=="--local-task-v1"', source)
        self.assertIn('bindLocalTaskParent(parent)', source)
        for marker in ('executeGameProbe(', 'sampleNativeSafetyTelemetry(', 'executeBillingInputQuery(', 'executeEnergyTraceRequest(', 'executeCreditRequest('):
            self.assertNotIn(marker, source)


if __name__ == '__main__':
    unittest.main()
