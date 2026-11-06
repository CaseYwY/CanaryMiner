"""Original energy scopes are memory-only and do not activate collection or ship fault hooks."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class EnergyNativeArtifacts(unittest.TestCase):
    def test_all_five_layers_bind_original_lifetimes(self):
        for name in ('energy_history_owner', 'energy_authorization_owner', 'energy_capture', 'energy_leased_capture', 'energy_runtime'):
            with self.subTest(name=name):
                text = (ROOT / ('src/product/' + name + '.cpp')).read_text(encoding='utf8')
                for marker in ('NativeProcessLifetime', 'NativeProcessScope original', 'nativeCurrent()', '.begin()', '.permitted()'):
                    self.assertIn(marker, text)
                for marker in ('__wrap_', 'RTLD_NEXT', 'failAllocations', 'canaryNativeAllocationFault', 'OM_TEST_ENERGY_NATIVE_LIFETIME'):
                    self.assertNotIn(marker, text)

    def test_uncertain_originals_and_preallocated_unavailable_views_remain(self):
        history = (ROOT / 'src/product/energy_history_owner.cpp').read_text(encoding='utf8')
        authorization = (ROOT / 'src/product/energy_authorization_owner.cpp').read_text(encoding='utf8')
        leased = (ROOT / 'src/product/energy_leased_capture.cpp').read_text(encoding='utf8')
        for marker in ('nativeCreationUncertain', 'result.writeOutcomeUnknown', 'emptyTerminal=published'):
            self.assertIn(marker, history)
        for marker in ('nativeUncertain==job', 'result.writeOutcomeUnknown', 'emptyTerminal=published'):
            self.assertIn(marker, authorization)
        self.assertIn('if(!started&&!issued)', leased)
        self.assertIn('current_.provisioningOutcomeUnknown=true', leased)

    def test_fixture_never_executes_storage_and_dispatches_before_parent(self):
        source = (ROOT / 'tests/test_energy_native_lifetime.cpp').read_text(encoding='utf8')
        self.assertIn('argc>=2&&std::string(argv[1])=="--local-task-v1"', source)
        self.assertIn('unknown helper modes cannot recurse', source)
        self.assertIn('bindLocalTaskParent(lease.parentPid)', source)
        for marker in ('executeEnergyHistoryRequest(', 'executeEnergyAuthorizationRequest(', 'serveEnergyCollectorLease(', 'executeEnergyCollectorGuard('):
            self.assertNotIn(marker, source)


if __name__ == '__main__':
    unittest.main()
