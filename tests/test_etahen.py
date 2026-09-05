"""Wire-level etaHEN DPI v2 tests against the actual C client and worker."""
import pathlib
import socket
import struct
import subprocess
import tempfile
import threading
import unittest
from urllib.parse import parse_qs, quote

ROOT = pathlib.Path(__file__).resolve().parents[1]


class EtaHEN(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.root = pathlib.Path(cls.tmp.name)
        src = cls.root / 'test.c'
        src.write_text(r'''
static int test_port;
#define ETAHEN_DPI_PORT test_port
#define ETAHEN_TIMEOUT_SECONDS 1
#define main payload_main
#include "app/romm_ui/main.c"
#undef main
int sceKernelSendNotificationRequest(int a, notify_request_t*b, size_t c, int d) { return 0; }
int main(int argc, char **argv) {
  signal(SIGPIPE, SIG_IGN);
  test_port = atoi(argv[1]);
  if (!strcmp(argv[2], "job")) {
    strcpy(transfer_job.cfg.host, "127.0.0.1");
    snprintf(transfer_job.cfg.port, sizeof transfer_job.cfg.port, "%s", argv[3]);
    transfer_job.rom_id = 7;
    transfer_job.platform_id = 1;
    transfer_job.saved_only = DOWNLOAD_ONLY;
    run_transfer(NULL);
    return strstr(transfer_job.message, "Download complete") ? 0 : 1;
  }
  char result[256];
  int rc = etahen_install(argv[2], argv[3], result, sizeof result);
  printf("RC=%d\n%s\n", rc, result);
  return 0;
}
''')
        cls.exe = cls.root / 'test'
        subprocess.run(['cc', '-Wall', '-Werror', '-pthread', '-I', str(ROOT),
                        f'-DDOWNLOAD_DIR="{cls.root}"', str(src), '-o', str(cls.exe)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def exchange(self, body=b'SUCCESS: Direct install console Task started',
                 status=200, length=None, fragmented=False, disconnect=False):
        server = socket.socket()
        server.bind(('127.0.0.1', 0))
        server.listen(1)
        server.settimeout(3)
        port = server.getsockname()[1]
        requests, errors = [], []
        def serve():
            try:
                with server, server.accept()[0] as conn:
                    conn.settimeout(3)
                    request = b''
                    while b'\r\n\r\n' not in request:
                        request += conn.recv(4096)
                    head, form = request.split(b'\r\n\r\n', 1)
                    size = int(next(x.split(b':', 1)[1] for x in head.split(b'\r\n')
                                    if x.startswith(b'Content-Length:')))
                    while len(form) < size:
                        form += conn.recv(4096)
                    requests.append((head, form))
                    if disconnect:
                        return
                    response = (f'HTTP/1.0 {status} Test\r\nContent-Length: '
                                f'{len(body) if length is None else length}\r\n\r\n').encode() + body
                    if fragmented:
                        for byte in response:
                            conn.sendall(bytes([byte]))
                    else:
                        conn.sendall(response)
            except Exception as exc:
                errors.append(exc)
        thread = threading.Thread(target=serve)
        thread.start()
        path = '/data/romm-ps5/downloads/[test] A+B & C %2F #?.pkg'
        result = subprocess.run([str(self.exe), str(port), path, 'A+B & C.pkg'],
                                capture_output=True, text=True, timeout=4)
        thread.join(4)
        self.assertFalse(thread.is_alive())
        self.assertFalse(errors, errors)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(len(requests), 1)
        head, form = requests[0]
        self.assertTrue(head.startswith(b'POST /upload HTTP/1.0\r\n'))
        self.assertNotIn(b'Authorization:', head)
        self.assertEqual(parse_qs(form.decode()), {
            'url': [f'http://127.0.0.1:{port}' + quote(path, safe='/')],
            'content_name': ['A+B & C.pkg']})
        return result.stdout

    def test_acceptance_and_form_encoding(self):
        self.assertIn('RC=0', self.exchange(fragmented=True))

    def test_http_200_failure_retains_exact_error(self):
        out = self.exchange(b'FAILED: Install failed (0x80B2116F)')
        self.assertIn('RC=1', out)
        self.assertIn('0x80B2116F', out)

    def test_unknown_truncated_and_http_failure_are_not_success(self):
        for kwargs in ({'body': b'<html>SUCCESS</html>'}, {'status': 500},
                       {'length': 10000}, {'disconnect': True}):
            with self.subTest(kwargs=kwargs):
                self.assertIn('RC=-2', self.exchange(**kwargs))

    def test_service_disabled(self):
        with socket.socket() as sock:
            sock.bind(('127.0.0.1', 0))
            port = sock.getsockname()[1]  # reserved but not listening
            result = subprocess.run([str(self.exe), str(port), '/test.pkg', 'test.pkg'],
                                    capture_output=True, text=True, timeout=3)
        self.assertIn('RC=-1', result.stdout)
        self.assertIn('Enable it in etaHEN', result.stdout)

    def test_download_only_worker_never_contacts_etahen(self):
        pkg = bytearray(0x500)
        pkg[:4] = b'\x7fCNT'
        cid = b'UP0000-CUSA00001_00-ABCDEFGHIJKLMNOP'
        pkg[0x40:0x40 + len(cid)] = cid
        struct.pack_into('>Q', pkg, 0x430, len(pkg))
        requests, errors = [], []
        api, dpi = socket.socket(), socket.socket()
        api.bind(('127.0.0.1', 0)); api.listen(3); api.settimeout(4)
        dpi.bind(('127.0.0.1', 0)); dpi.listen(1); dpi.settimeout(0.1)
        def serve():
            try:
                for body in (b'[{"id":1,"slug":"ps4"}]', b'{"fs_name":"download-only.pkg"}', pkg):
                    with api.accept()[0] as conn:
                        conn.settimeout(3)
                        req = b''
                        while b'\r\n\r\n' not in req:
                            req += conn.recv(4096)
                        requests.append(req.split(b'\r\n')[0])
                        conn.sendall(f'HTTP/1.0 200 OK\r\nContent-Length: {len(body)}\r\n\r\n'.encode() + body)
            except Exception as exc:
                errors.append(exc)
        thread = threading.Thread(target=serve)
        thread.start()
        try:
            result = subprocess.run([str(self.exe), str(dpi.getsockname()[1]), 'job',
                                     str(api.getsockname()[1])], capture_output=True, text=True, timeout=5)
            thread.join(4)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('No installation requested', result.stdout)
            self.assertEqual((self.root / 'download-only.pkg').read_bytes(), pkg)
            self.assertEqual(len(requests), 3)
            with self.assertRaises(socket.timeout):
                dpi.accept()
            self.assertFalse(errors, errors)
        finally:
            api.close(); dpi.close()
            thread.join(4)
