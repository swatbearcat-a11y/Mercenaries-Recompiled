"""Exercise the multiplayer transport (ENet on its own thread) over loopback."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
ENET = ROOT / 'ports/mercenaries/third_party/enet'


class MultiplayerNet(unittest.TestCase):
    def test_loopback_session(self):
        compiler = shutil.which('gcc') or 'C:/MinGW/bin/gcc.exe'
        if not Path(compiler).is_file() and not shutil.which(compiler):
            self.skipTest('Requires GCC')
        sources = [str(ENET / name) for name in ('callbacks.c', 'compress.c', 'host.c', 'list.c', 'packet.c',
                                                  'peer.c', 'protocol.c', 'win32.c')]
        with tempfile.TemporaryDirectory(prefix='merc-mp-net-') as directory:
            exe = Path(directory) / 'test.exe'
            subprocess.run([compiler, '-std=c11', '-O2', '-I', str(ENET / 'include'),
                            str(ROOT / 'tools/recomp/fixtures/multiplayer_net.c'), *sources,
                            '-o', str(exe), '-lws2_32', '-lwinmm'], check=True)
            subprocess.run([str(exe)], check=True, timeout=120)

    def test_off_by_default(self):
        source = (ROOT / 'ports/mercenaries/src/multiplayer_net.c').read_text(encoding='utf-8')
        self.assertIn('GetPrivateProfileStringA("Multiplayer", "mode", "off"', source)


if __name__ == '__main__':
    unittest.main()
