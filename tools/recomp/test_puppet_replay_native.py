"""Exercise the multiplayer M1 puppet replay experiment without running the game."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class PuppetReplay(unittest.TestCase):
    def test_spawn_replay_and_corrections(self):
        compiler = shutil.which('gcc') or 'C:/MinGW/bin/gcc.exe'
        if not Path(compiler).is_file() and not shutil.which(compiler):
            self.skipTest('Requires GCC')
        with tempfile.TemporaryDirectory(prefix='merc-puppet-replay-') as directory:
            exe = Path(directory) / 'test.exe'
            subprocess.run([compiler, '-std=c11', '-O2',
                            '-I', str(ROOT / 'ports/mercenaries/src'),
                            '-I', str(ROOT / 'src/input'), '-I', str(ROOT / 'src'),
                            str(ROOT / 'tools/recomp/fixtures/puppet_replay.c'),
                            '-o', str(exe), '-lm'], check=True)
            subprocess.run([str(exe)], check=True)

    def test_control_recorder_patch(self):
        patcher = (ROOT / 'ports/mercenaries/scripts/Patch-Generated.py').read_text(encoding='utf-8')
        self.assertIn('Multiplayer player control recorder', patcher)
        self.assertIn('recomp_multiplayer_player_control(ecx,MEM32(esp+4u),MEM32(esp+8u)); }', patcher)


if __name__ == '__main__':
    unittest.main()
