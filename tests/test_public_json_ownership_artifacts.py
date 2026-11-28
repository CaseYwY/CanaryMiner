"""The source boundary is module-local, parent-owned and not an injectable shipping test API."""
from pathlib import Path
import unittest


class PublicJsonOwnershipArtifacts(unittest.TestCase):
    def test_stable_identity_and_retained_owner(self):
        root = Path(__file__).resolve().parents[1]
        source = (root / 'src/product/public_json_process.cpp').read_text(encoding='utf-8')
        self.assertNotIn('::kill(', source)
        self.assertNotIn('waitpid(', source)
        self.assertNotIn('posix_spawn(&', source)
        self.assertNotIn('SYS_pidfd_open', source)
        self.assertIn('pidfd_spawn(&pidfd', source)
        self.assertIn('waitid(P_PIDFD', source)
        self.assertIn('SYS_pidfd_send_signal, pidfd, SIGKILL', source)
        self.assertIn('if (unknown)', source)
        self.assertIn('held.reserve(64)', source)
        self.assertIn('Impl::quarantine().push_back(impl_.release())', source)
        self.assertNotIn('CANARY_PUBLIC_JSON_TESTING', source)
        self.assertNotIn('__wrap_', source)

    def test_parent_only_outcome_is_not_a_helper_claim(self):
        root = Path(__file__).resolve().parents[1]
        source = (root / 'src/product/public_json_process.cpp').read_text(encoding='utf-8')
        # Both encode and decode use the same allowlist. The original parent-only value remains excluded.
        self.assertIn('status <= static_cast<unsigned>(PublicHttpsStatus::InvalidJson)', source)
        self.assertIn('status == static_cast<unsigned>(PublicHttpsStatus::AssetIdentity)', source)
        self.assertEqual(source.count('!wireStatus('), 2)
        self.assertIn('// Cleanup is part of the result', source)
        header = (root / 'src/product/public_json_process.h').read_text(encoding='utf-8')
        self.assertNotIn('TestPeer', header)
        self.assertNotIn('__wrap_', header)
        entry = (root / 'src/core/miner.cu').read_text(encoding='utf-8')
        self.assertNotIn('marketProcessFetch(', entry)


if __name__ == '__main__':
    unittest.main()
