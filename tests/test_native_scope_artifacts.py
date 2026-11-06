"""Original memory scopes do not add native authority, automatic activation or shipping test seams."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class NativeScopeArtifacts(unittest.TestCase):
    def test_memory_only_scope_and_original_lifetime(self):
        source = (ROOT / 'src/product/native_process_admission.h').read_text(encoding='utf8')
        for marker in ('thread_local ScopeState', 'NativeProcessScope', 'NativeProcessLifetime',
                       'epoch_=nativeProcessAdmissionEpoch()', 'scopePermits(epoch_)', 'value.epoch=previous_.active'):
            self.assertIn(marker, source)
        for marker in ('<thread>', '<mutex>', '<filesystem>', 'getenv(', 'CreateProcess', 'syscall(', 'TestPeer', 'LD_PRELOAD'):
            self.assertNotIn(marker, source)

    def test_credit_lifetime_precedes_owned_thread_work(self):
        source = (ROOT / 'src/product/credit_owner.cpp').read_text(encoding='utf8')
        for marker in ('NativeProcessLifetime nativeLife', 'NativeProcessScope original(nativeLife.epoch())',
                       'if(!s.nativeLife.begin())', '!nativeLife.permitted()', 'if(!nativeLife.current())',
                       'result.writeOutcomeUnknown', 'nativeUncertain==job', 'emptyTerminal'):
            self.assertIn(marker, source)
        self.assertNotIn('__wrap_', source)
        worker = (ROOT / 'src/product/worker_process_owner.cpp').read_text(encoding='utf8')
        intent = (ROOT / 'src/product/session_intent.h').read_text(encoding='utf8')
        self.assertIn('p.nativeEpoch=nativeProcessAdmissionEpoch()', worker)
        self.assertIn('NativeProcessScope original(nativeEpoch)', worker)
        self.assertIn('nativeProcessAdmissionEpoch()==nativeEpoch_', intent)

    def test_no_ordinary_credit_activation_or_recursive_fixture_fallthrough(self):
        entry = (ROOT / 'src/core/miner.cu').read_text(encoding='utf8')
        self.assertNotIn('CreditLedgerOwner(', entry)
        fixture = (ROOT / 'tests/test_credit_queued_native_admission.cpp').read_text(encoding='utf8')
        self.assertIn('argc>=2&&std::string(argv[1])=="--local-task-v1"', fixture)
        self.assertIn('return 4; // never recurse', fixture)
        self.assertNotIn('executeCreditRequest(', fixture)


if __name__ == '__main__':
    unittest.main()
