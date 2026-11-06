"""Ownership repairs do not install fault APIs, activate sources or claim application-wide quarantine."""
from pathlib import Path
import unittest


class LocalHelperArtifacts(unittest.TestCase):
    def test_local_ownership_is_creation_bound_and_retained(self):
        root = Path(__file__).resolve().parents[1]
        source = (root / 'src/product/local_task_process.cpp').read_text(encoding='utf-8')
        for marker in ('waitpid(', '::kill(', 'SYS_pidfd_open'):
            self.assertNotIn(marker, source)
        self.assertIn('pidfd_spawn(&pidfd', source)
        self.assertIn('waitid(P_PIDFD', source)
        self.assertIn('held.reserve(64)', source)
        self.assertIn('Impl::quarantine().push_back(impl_.release())', source)
        self.assertEqual(source.count('result.childStarted=impl_->created'), 2)
        self.assertEqual(source.count('!impl_->stop()||Impl::unresolved().load()'), 2)

    def test_resource_owners_are_not_shipping_injection(self):
        root = Path(__file__).resolve().parents[1]
        for name in ('local_task_process', 'public_json_process'):
            source = (root / 'src/product' / (name + '.cpp')).read_text(encoding='utf-8')
            self.assertIn('AttributeCleanup', source)
            self.assertIn('struct Setup', source)
            for marker in ('faultCreatePipe', 'refuseSpawn', 'OM_NATIVE_FAULT_LOCAL', 'failNew'):
                self.assertNotIn(marker, source)
        entry = (root / 'src/core/miner.cu').read_text(encoding='utf-8')
        self.assertNotIn('marketProcessFetch(', entry)


if __name__ == '__main__':
    unittest.main()
