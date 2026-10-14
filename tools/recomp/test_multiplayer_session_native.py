"""Exercise the M2 multiplayer session against stubbed guest memory and a fake transport."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class MultiplayerSession(unittest.TestCase):
    def test_session(self):
        compiler = shutil.which('gcc') or 'C:/MinGW/bin/gcc.exe'
        if not Path(compiler).is_file() and not shutil.which(compiler):
            self.skipTest('Requires GCC')
        with tempfile.TemporaryDirectory(prefix='merc-mp-session-') as directory:
            exe = Path(directory) / 'test.exe'
            subprocess.run([compiler, '-std=c11', '-O2',
                            '-I', str(ROOT / 'ports/mercenaries/src'),
                            '-I', str(ROOT / 'src/input'), '-I', str(ROOT / 'src'),
                            str(ROOT / 'tools/recomp/fixtures/multiplayer_session.c'),
                            '-o', str(exe), '-lm'], check=True)
            subprocess.run([str(exe)], check=True, timeout=120)


if __name__ == '__main__':
    unittest.main()
