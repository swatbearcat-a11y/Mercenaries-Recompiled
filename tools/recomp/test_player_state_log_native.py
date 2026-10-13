"""Exercise the multiplayer player-state logger (M0) without running the game."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class PlayerStateLog(unittest.TestCase):
    def test_sampling_layout_and_guest_context(self):
        compiler = shutil.which('gcc') or 'C:/MinGW/bin/gcc.exe'
        if not Path(compiler).is_file() and not shutil.which(compiler):
            self.skipTest('Requires GCC')
        with tempfile.TemporaryDirectory(prefix='merc-player-state-') as directory:
            exe = Path(directory) / 'test.exe'
            subprocess.run([compiler, '-std=c11', '-O2',
                            '-I', str(ROOT / 'ports/mercenaries/src'),
                            '-I', str(ROOT / 'src/input'), '-I', str(ROOT / 'src'),
                            str(ROOT / 'tools/recomp/fixtures/player_state_log.c'),
                            '-o', str(exe), '-lm'], check=True)
            subprocess.run([str(exe)], check=True)

    def test_world_checkpoint_patch(self):
        patcher = (ROOT / 'ports/mercenaries/scripts/Patch-Generated.py').read_text(encoding='utf-8')
        call = "recomp_multiplayer_tick(ecx,MEM32(esp+4u)); }\\n\\nloc_001702D0: ;"
        # The insertion and the translator-inlined form that a rerun accepts.
        self.assertEqual(patcher.count(call), 2)
        manual = (ROOT / 'ports/mercenaries/src/recomp_manual.c').read_text(encoding='utf-8')
        self.assertIn('#include "multiplayer_guest.h"', manual)


if __name__ == '__main__':
    unittest.main()
