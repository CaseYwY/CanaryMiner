"""Shared cooperative admission must not acquire OS authority, shipping fault hooks or ordinary activation."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class NativeAdmissionArtifacts(unittest.TestCase):
    def test_four_native_owners_share_the_fence_and_pinned_identity(self):
        for name in ('public_json_process', 'local_task_process', 'duplex_task_process', 'worker_process'):
            source = (ROOT / 'src/product' / (name + '.cpp')).read_text(encoding='utf8')
            for marker in ('native_process_admission.h', 'NativeProcessAdmission admission', 'admission.markUnresolved()',
                           'admission.permitted()', 'admission.invalidated()', 'pidfd_spawn(&pidfd', 'waitid(P_PIDFD'):
                self.assertIn(marker, source)
            for marker in ('::kill(', 'waitpid(', 'SYS_pidfd_open', 'faultCreatePipe', '__wrap_', 'canaryNativeAllocationFault'):
                self.assertNotIn(marker, source)

    def test_shared_state_has_no_native_or_test_dependencies(self):
        source = (ROOT / 'src/product/native_process_admission.h').read_text(encoding='utf8')
        for marker in ('<thread>', '<mutex>', '<filesystem>', '<vector>', 'windows.h', 'syscall(', 'dlopen(',
                       'getenv(', 'TestPeer', '__wrap_', 'fork(', 'CreateProcess', 'cudaGet', 'nvmlDevice'):
            self.assertNotIn(marker, source)
        self.assertIn('compare_exchange_weak', source)
        self.assertIn('Intentionally no destructor decrement', source)

    def test_original_control_epochs_are_not_fresh_request_receipts(self):
        intent = (ROOT / 'src/product/session_intent.h').read_text(encoding='utf8')
        owner = (ROOT / 'src/product/worker_process_owner.cpp').read_text(encoding='utf8')
        supervisor = (ROOT / 'src/product/supervisor.h').read_text(encoding='utf8')
        self.assertIn('nativeProcessEpochCurrent(nativeEpoch_)', intent)
        self.assertIn('p.nativeEpoch=nativeProcessEpoch()', owner)
        self.assertIn('!nativeProcessEpochCurrent(nativeEpoch)', owner)
        self.assertGreaterEqual(supervisor.count('latchNativeFault();'), 5)

    def test_no_ordinary_factory_or_shipping_allocator_replacement(self):
        entry = (ROOT / 'src/core/miner.cu').read_text(encoding='utf8')
        self.assertNotIn('marketProcessFetch(', entry)
        self.assertNotIn('PoolTlsProcessTransport(', entry)
        for document in (ROOT / 'cmake').glob('*.cmake'):
            source = document.read_text(encoding='utf8')
            self.assertNotIn('native_allocation_faults.cpp', source)
            self.assertNotIn('test_cross_helper_admission.cpp', source)


if __name__ == '__main__':
    unittest.main()
