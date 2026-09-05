"""Exercise real diagnostic parser with bounded and malformed PKG fixtures."""
import pathlib
import struct
import subprocess
import tempfile
import unittest
from build_support import MINIZ_ARGS
from http.server import BaseHTTPRequestHandler, HTTPServer
from threading import Thread

ROOT = pathlib.Path(__file__).resolve().parents[1]
CID = b'UP0000-CUSA00001_00-ABCDEFGHIJKLMNOP'


class Diagnostics(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.root = pathlib.Path(cls.tmp.name)
        src = cls.root / 'test.c'
        src.write_text(r'''
#define main payload_main
#include "app/romm_ui/main.c"
#undef main
int sceKernelSendNotificationRequest(int a, notify_request_t*b, size_t c, int d) { return 0; }
int main(int argc, char **argv) {
  if (!strcmp(argv[1], "job")) {
    strcpy(transfer_job.cfg.host, "127.0.0.1");
    snprintf(transfer_job.cfg.port, sizeof transfer_job.cfg.port, "%s", argv[2]);
    transfer_job.rom_id = 7;
    transfer_job.platform_id = 1;
    transfer_job.saved_only = INSPECT_SAVED;
    run_transfer(NULL);
    return strstr(transfer_job.message, "Inspection complete") ? 0 : 2;
  }
  char cid[49];
  if (inspect_pkg(argv[1], cid)) return 2;
  return diagnose_pkg(argv[1], cid) ? 1 : 0;
}
''')
        cls.exe = cls.root / 'test'
        subprocess.run(['cc', *MINIZ_ARGS, '-Wall', '-Werror', '-pthread', '-I', str(ROOT),
                        f'-DDOWNLOAD_DIR="{cls.root}"', str(src), '-o', str(cls.exe)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def fixture(self):
        p = bytearray(0x800)
        p[:4] = b'\x7fCNT'
        p[0x40:0x40 + len(CID)] = CID
        struct.pack_into('>Q', p, 0x430, len(p))
        struct.pack_into('>I', p, 0x10, 1)
        struct.pack_into('>I', p, 0x18, 0x500)
        struct.pack_into('>I', p, 0x500, 0x1001)
        struct.pack_into('>II', p, 0x510, 0x600, 416)
        p[0x600:0x604] = b'plgo'
        struct.pack_into('<HHHHI', p, 0x608, 1, 1, 1, 1, 416)
        p[0x640:0x640 + len(CID)] = CID
        for i, (off, length) in enumerate([(256,32), (288,2), (304,9),
                (320,16), (352,32), (384,2), (400,12), (336,16)]):
            struct.pack_into('<II', p, 0x6c0 + i * 8, off, length)
        struct.pack_into('<HH', p, 0x600 + 352 + 20, 1, 1)
        return p

    def check(self, data, expected, message):
        path = self.root / 'fixture.pkg'
        path.write_bytes(data)
        result = subprocess.run([str(self.exe), str(path)], capture_output=True, text=True, timeout=3)
        self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
        self.assertIn(message, result.stdout)
        self.assertEqual(path.read_bytes(), data)

    def test_valid_structure_does_not_claim_authenticity(self):
        self.check(self.fixture(), 0, 'retail/FPKG=unknown')

    def test_bad_table_and_entry_bounds(self):
        p = self.fixture()
        struct.pack_into('>I', p, 0x18, 0xfffffff0)
        self.check(p, 1, 'entry table invalid')
        p = self.fixture()
        struct.pack_into('>I', p, 0x514, 0xffffffff)
        self.check(p, 1, 'bounds=INVALID')

    def test_bad_playgo_sections_and_scenario_reference(self):
        p = self.fixture()
        struct.pack_into('<I', p, 0x6c0, 0xfffffff0)
        self.check(p, 1, 'bounds=INVALID')
        p = self.fixture()
        struct.pack_into('<H', p, 0x600 + 384, 1)
        self.check(p, 1, 'references a missing chunk')

    def test_encrypted_and_missing_playgo(self):
        p = self.fixture()
        struct.pack_into('>I', p, 0x508, 0x80000000)
        self.check(p, 1, 'encrypted; contents not inspected')
        struct.pack_into('>I', p, 0x500, 0x1000)
        self.check(p, 1, 'found 0')

    def test_inspect_job_never_installs_or_downloads(self):
        path = self.root / 'fixture.pkg'
        data = self.fixture()
        path.write_bytes(data)
        requests = []
        class Handler(BaseHTTPRequestHandler):
            def do_GET(self):
                requests.append(self.path)
                body = {'/api/platforms': b'[{"id":1,"slug":"ps4"}]',
                        '/api/roms/7': b'{"fs_name":"fixture.pkg"}'}.get(self.path)
                self.send_response(200 if body else 404)
                self.end_headers()
                self.wfile.write(body or b'')
            def log_message(self, *args):
                pass
        server = HTTPServer(('127.0.0.1', 0), Handler)
        thread = Thread(target=server.serve_forever)
        thread.start()
        try:
            result = subprocess.run([str(self.exe), 'job', str(server.server_port)],
                                    capture_output=True, text=True, timeout=5)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual(requests, ['/api/platforms', '/api/roms/7'])
            self.assertIn('No installation requested', result.stdout)
            self.assertEqual(path.read_bytes(), data)
        finally:
            server.shutdown()
            thread.join()
            server.server_close()
