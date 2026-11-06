"""Remaining background scopes are memory revocation, never shipping test hooks or qualified device adapters."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class BackgroundNativeArtifacts(unittest.TestCase):
    def test_three_background_owners_bind_original_scopes(self):
        for unit in ('energy_owner.cpp', 'game_worker_session.cpp', 'supervisor_sampling.h'):
            with self.subTest(unit=unit):
                source = (ROOT / 'src/product' / unit).read_text(encoding='utf8')
                for marker in ('NativeProcessLifetime', 'NativeProcessScope original', '.begin()'):
                    self.assertIn(marker, source)
                for marker in ('__wrap_', 'RTLD_NEXT', 'canaryNativeAllocationFault', 'cpu_telemetry_fixture', 'OM_TEST_'):
                    self.assertNotIn(marker, source)

    def test_sampling_getters_invoke_no_source_or_native_adapter(self):
        source = (ROOT / 'src/product/supervisor_sampling.h').read_text(encoding='utf8')
        getter = source.split('std::shared_ptr<const DisplaySampleBatch> take()', 1)[1].split('void stop()', 1)[0]
        self.assertIn('nativeCurrent()', getter)
        for marker in ('factory_(', 'reader->read', 'nvml.', 'httpGetLocal(', '.join('):
            self.assertNotIn(marker, getter)

    def test_session_energy_masks_canceled_unpublished_running_views(self):
        source = (ROOT / 'src/product/energy_owner.cpp').read_text(encoding='utf8')
        getter = source.split('std::shared_ptr<const SessionEnergySnapshot> snapshot()const override', 1)[1]
        for marker in ('nativeCurrent()', 'nativeLife_.permitted()', 'SessionEnergyState::Running?s.empty_', 's.cancel_'):
            self.assertIn(marker, getter)

    def test_helpers_dispatch_before_parent_owners_and_never_execute_real_adapters(self):
        for unit, owner in (('test_game_identity_native_lifetime.cpp', 'SessionIntent intent;'),
                            ('test_sampling_native_lifetime.cpp', 'SupervisorSampling owner;')):
            with self.subTest(unit=unit):
                source = (ROOT / 'tests' / unit).read_text(encoding='utf8')
                self.assertLess(source.index('"--local-task-v1"'), source.index(owner))
                for marker in ('executeGameProbe(', 'sampleNativeSafetyTelemetry(', 'privateEntropy(', 'NativeDisplaySampleReader reader'):
                    self.assertNotIn(marker, source)
        self.assertIn('#include "cpu_telemetry_fixture.h"', (ROOT / 'tests/test_sampling_native_lifetime.cpp').read_text(encoding='utf8'))


if __name__ == '__main__':
    unittest.main()
