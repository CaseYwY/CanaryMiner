"""Ordinary native ownership stays pinned, cooperative, header-only and free of shipping fault hooks."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class LegacyChildArtifacts(unittest.TestCase):
    def test_creation_identity_and_fixed_retention(self):
        source = (ROOT / 'src/product/legacy_child_process.h').read_text(encoding='utf8')
        for marker in ('pidfd_spawn(&pidfd', 'waitid(P_PIDFD', 'SYS_pidfd_send_signal', 'std::array<Owner *,256>',
                       'admission.markUnresolved()', 'admission.settled()', 'if(unknown)return false;'):
            self.assertIn(marker, source)
        for marker in ('fork(', 'waitpid(', '::kill(', 'SYS_pidfd_open', '__wrap_', 'canaryNativeAllocationFault'):
            self.assertNotIn(marker, source)

    def test_console_and_scoped_resources(self):
        source = (ROOT / 'src/product/legacy_child_process.h').read_text(encoding='utf8')
        for marker in ('CREATE_SUSPENDED', 'AssignProcessToJobObject', 'JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE',
                       'PROC_THREAD_ATTRIBUTE_HANDLE_LIST', 'DeleteProcThreadAttributeList', 'posix_spawnattr_destroy',
                       'posix_spawn_file_actions_destroy', 'PIPE_NOWAIT', 'O_NONBLOCK', 'readerStop', 'readerDrain'):
            self.assertIn(marker, source)
        for marker in ('POSIX_SPAWN_SETPGROUP', 'CREATE_NEW_CONSOLE', 'CREATE_NEW_PROCESS_GROUP', 'DETACHED_PROCESS'):
            self.assertNotIn(marker, source)

    def test_supervisor_uses_original_stop_and_unicode_path(self):
        source = (ROOT / 'src/product/supervisor.h').read_text(encoding='utf8')
        self.assertIn('#include "legacy_child_process.h"', source)
        self.assertIn('#include "executable_path.h"', source)
        self.assertIn('selfExecutablePath()', source)
        path = (ROOT / 'src/product/executable_path.h').read_text(encoding='utf8')
        self.assertIn('GetModuleFileNameW', path)
        self.assertIn('std::filesystem::path(std::wstring(buf, n)).u8string()', path)
        for fallback in ('GetModuleFileNameA', 'getenv(', 'argv[', 'canonical(', 'exists('):
            self.assertNotIn(fallback, path)
        self.assertIn('&err,[this]{if(opt_.stop&&*opt_.stop)intent_.stop();return !intent_.permits(sessionToken_);}', source)
        for marker in ('class ChildProcess', 'CreateProcessA', 'waitpid(', '::kill('):
            self.assertNotIn(marker, source)


if __name__ == '__main__':
    unittest.main()
