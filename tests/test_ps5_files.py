"""Real ZIP/ZIP64 reader tests, plus PS5 worker routing through a mock RomM."""
import io
import json
import pathlib
import stat
import struct
import subprocess
import tempfile
import threading
import unittest
import warnings
import zipfile
from http.server import BaseHTTPRequestHandler, HTTPServer
from build_support import MINIZ_ARGS

ROOT = pathlib.Path(__file__).resolve().parents[1]


class PS5Files(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory()
        src = pathlib.Path(cls.build.name) / 'test.c'
        src.write_text(r'''
static char game_dir[512], stage_dir[512], download_dir[512];
#define PS5_GAME_DIR game_dir
#define PS5_STAGE_DIR stage_dir
#define DOWNLOAD_DIR download_dir
#define main payload_main
#include "app/romm_ui/main.c"
#undef main
int sceKernelSendNotificationRequest(int a, notify_request_t*b, size_t c, int d) { return 0; }
int main(int argc, char **argv) {
  snprintf(game_dir, sizeof game_dir, "%s/games", argv[1]);
  snprintf(stage_dir, sizeof stage_dir, "%s/stage", argv[1]);
  snprintf(download_dir, sizeof download_dir, "%s", argv[1]);
  if (!strcmp(argv[2], "job")) {
    strcpy(transfer_job.cfg.host, "127.0.0.1");
    snprintf(transfer_job.cfg.port, sizeof transfer_job.cfg.port, "%s", argv[3]);
    transfer_job.rom_id = 7; transfer_job.platform_id = 2;
    transfer_job.saved_only = atoi(argv[4]);
    run_transfer(NULL);
    return 0;
  }
  char result[256];
  const char *name = strrchr(argv[2], '/');
  int rc = ps5_prepare(argv[2], name ? name + 1 : argv[2], 7, result, sizeof result);
  puts(result);
  return rc ? 1 : 0;
}
''')
        cls.exe = pathlib.Path(cls.build.name) / 'test'
        subprocess.run(['cc', *MINIZ_ARGS, '-Wall', '-Werror', '-pthread', '-I', str(ROOT),
                        str(src), '-o', str(cls.exe)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.build.cleanup()

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = pathlib.Path(self.tmp.name)

    def archive(self, prefix='', extra=(), compression=zipfile.ZIP_DEFLATED, zip64=False):
        buf = io.BytesIO()
        with warnings.catch_warnings(), zipfile.ZipFile(buf, 'w', compression=compression) as z:
            warnings.simplefilter('ignore', UserWarning)
            for name, data in [(prefix + 'eboot.bin', b'GAME_EXECUTABLE'),
                               (prefix + 'sce_sys/param.json', b'{"titleId":"PPSA00001"}'),
                               (prefix + 'assets/test.bin', b'abc123' * 10000), *extra]:
                if zip64:
                    with z.open(name, 'w', force_zip64=True) as f:
                        f.write(data)
                else:
                    z.writestr(name, data)
        return buf.getvalue()

    def prepare(self, data, success=True, filename='game.zip', timeout=8):
        path = self.root / filename
        path.write_bytes(data)
        result = subprocess.run([str(self.exe), str(self.root), str(path)],
                                capture_output=True, text=True, timeout=timeout)
        self.assertEqual(result.returncode, 0 if success else 1, result.stdout + result.stderr)
        self.assertEqual(path.read_bytes(), data)
        self.assertEqual(list((self.root / 'stage').iterdir()), [])
        if not success:
            self.assertFalse((self.root / 'games/romm-7').exists())
        return result

    def test_nested_deflated_game_is_flattened(self):
        self.prepare(self.archive('Outer/Title/PPSA00001-app0/'))
        game = self.root / 'games/romm-7'
        self.assertEqual((game / 'eboot.bin').read_bytes(), b'GAME_EXECUTABLE')
        self.assertEqual((game / 'assets/test.bin').read_bytes(), b'abc123' * 10000)

    def test_root_stored_zip64(self):
        self.prepare(self.archive(compression=zipfile.ZIP_STORED, zip64=True))
        game = self.root / 'games/romm-7'
        self.assertTrue((game / 'sce_sys/param.json').exists())
        # Archive-root games (no wrapper folder) publish the mkdtemp() staging
        # directory itself; it must not keep mkdtemp's private 0700 mode or
        # the mounter can discover the tile but never read into it to launch.
        self.assertEqual(stat.S_IMODE(game.stat().st_mode), 0o755)

    def test_large_zip64_game_entry_count(self):
        # Match the reported ASTRO BOT entry count with small synthetic files.
        data = self.archive(extra=((f'assets/file-{i}.bin', b'x') for i in range(158202)))
        result = self.prepare(data, timeout=120)
        self.assertIn('preflight entries=158205', result.stdout)
        self.assertEqual((self.root / 'games/romm-7/assets/file-158201.bin').read_bytes(), b'x')

    def test_empty_zip_diagnostic(self):
        buf = io.BytesIO()
        with zipfile.ZipFile(buf, 'w'):
            pass
        self.assertIn('ZIP is empty', self.prepare(buf.getvalue(), success=False).stdout)

    def test_traversal_and_absolute_paths(self):
        for name in ('../escape', '/escape', 'C:/escape', 'a/../../escape', 'a\\escape', './escape'):
            with self.subTest(name=name):
                self.prepare(self.archive(extra=[(name, b'bad')]), success=False)

    def test_symlink_entry(self):
        entry = zipfile.ZipInfo('link')
        entry.create_system = 3
        entry.external_attr = (stat.S_IFLNK | 0o777) << 16
        self.prepare(self.archive(extra=[(entry, b'/tmp')]), success=False)

    def test_duplicate_file_does_not_publish_partial_game(self):
        self.prepare(self.archive(extra=[('assets/test.bin', b'duplicate')]), success=False)

    def test_corrupt_crc_and_truncated_zip(self):
        data = self.archive(compression=zipfile.ZIP_STORED)
        self.prepare(data.replace(b'GAME_EXECUTABLE', b'BAD_EXECUTABLE!'), success=False)
        self.prepare(data[:-40], success=False)

    def test_multiple_games_or_missing_eboot(self):
        self.prepare(self.archive('one/', extra=[('two/sce_sys/param.json', b'{}')]), success=False)
        buf = io.BytesIO()
        with zipfile.ZipFile(buf, 'w') as z:
            z.writestr('sce_sys/param.json', b'{}')
        self.prepare(buf.getvalue(), success=False)

    def test_unsupported_compression_and_encryption(self):
        self.prepare(self.archive(compression=zipfile.ZIP_BZIP2), success=False)
        data = bytearray(self.archive())
        struct.pack_into('<H', data, 6, struct.unpack_from('<H', data, 6)[0] | 1)
        central = data.index(b'PK\x01\x02')
        struct.pack_into('<H', data, central + 8, struct.unpack_from('<H', data, central + 8)[0] | 1)
        self.prepare(data, success=False)

    def test_images_raw_and_zipped(self):
        self.prepare(b'image-bytes', filename='game.ffpkg')
        final = self.root / 'games/romm-7.ffpkg'
        self.assertEqual(final.read_bytes(), b'image-bytes')
        self.assertEqual(final.stat().st_ino, (self.root / 'game.ffpkg').stat().st_ino)
        buf = io.BytesIO()
        with zipfile.ZipFile(buf, 'w') as z:
            z.writestr('wrapper/game.exfat', b'exfat-bytes')
        self.prepare(buf.getvalue())
        self.assertEqual((self.root / 'games/romm-7.exfat').read_bytes(), b'exfat-bytes')

    def test_existing_game_is_not_overwritten(self):
        self.prepare(self.archive())
        game = self.root / 'games/romm-7'
        (game / 'keep').write_text('untouched')
        result = subprocess.run([str(self.exe), str(self.root), str(self.root / 'game.zip')], capture_output=True)
        self.assertEqual(result.returncode, 1)
        self.assertEqual((game / 'keep').read_text(), 'untouched')

    def test_ps5_worker_download_and_prepare(self):
        data = self.archive('PPSA00001-app0/')
        requests = []
        class Handler(BaseHTTPRequestHandler):
            def do_GET(self):
                requests.append(self.path)
                if self.path == '/api/platforms':
                    body = b'[{"id":1,"slug":"ps4"},{"id":2,"slug":"ps5"}]'
                elif self.path == '/api/roms/7':
                    body = b'{"fs_name":"game.zip"}'
                else:
                    body = data
                self.send_response(200)
                self.send_header('Content-Length', str(len(body)))
                self.end_headers()
                self.wfile.write(body)
            def log_message(self, *args):
                pass
        server = HTTPServer(('127.0.0.1', 0), Handler)
        thread = threading.Thread(target=server.serve_forever)
        thread.start()
        try:
            # Download-only must not extract; the next action reuses that ZIP.
            for mode in ('3', '1'):
                result = subprocess.run([str(self.exe), str(self.root), 'job', str(server.server_port), mode],
                                        capture_output=True, text=True, timeout=8)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertNotIn('[etahen]', result.stdout)
                if mode == '3':
                    self.assertIn('not extracted or prepared', result.stdout)
                    self.assertFalse((self.root / 'games/romm-7').exists())
                else:
                    self.assertIn('[ps5] prepared=', result.stdout)
            self.assertEqual(sum('/content/' in x for x in requests), 1)
            self.assertEqual((self.root / 'game.zip').read_bytes(), data)
            self.assertTrue((self.root / 'games/romm-7/eboot.bin').exists())
        finally:
            server.shutdown(); thread.join(); server.server_close()
