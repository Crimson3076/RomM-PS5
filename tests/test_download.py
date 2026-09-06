"""Host regression tests for the actual C downloader; no PS5 SDK needed."""
import io
import zipfile
import pathlib
import socket
import subprocess
import tempfile
import threading
import unittest
from build_support import MINIZ_ARGS

ROOT = pathlib.Path(__file__).resolve().parents[1]


class Downloads(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.root = pathlib.Path(cls.tmp.name)
        harness = cls.root / 'harness.c'
        harness.write_text('''
#define main payload_main
#include "app/romm_ui/main.c"
#undef main
int sceKernelSendNotificationRequest(int a, notify_request_t*b, size_t c, int d) { return 0; }
int main(int argc, char **argv) {
  config_t cfg = {0};
  strcpy(cfg.host, "127.0.0.1");
  snprintf(cfg.port, sizeof cfg.port, "%s", argv[1]);
  return romm_download_to_file_mode(&cfg, "test", argv[3], argv[2], argc > 4) == 0 ? 0 : 1;
}
''')
        cls.exe = cls.root / 'download'
        subprocess.run(['cc', *MINIZ_ARGS, '-Wall', '-Werror', '-I', str(ROOT), str(harness), '-o', str(cls.exe)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def transfer(self, response, success, body=b'', fragmented=False, path='/test.pkg', generated=False):
        dest = self.root / 'test.pkg'
        dest.write_bytes(b'previous complete file')
        server = socket.socket()
        server.bind(('127.0.0.1', 0))
        server.listen(1)
        requests = []
        def serve():
            with server:
                conn, _ = server.accept()
                with conn:
                    request = b''
                    while b'\r\n\r\n' not in request:
                        request += conn.recv(4096)
                    requests.append(request)
                    try:
                        if fragmented:
                            for byte in response:
                                conn.sendall(bytes([byte]))
                        else:
                            conn.sendall(response)
                    except (BrokenPipeError, ConnectionResetError):
                        pass
        thread = threading.Thread(target=serve)
        thread.start()
        result = subprocess.run([str(self.exe), str(server.getsockname()[1]), str(dest), path] + (["generated"] if generated else []), capture_output=True, timeout=5)
        thread.join(timeout=5)
        self.assertFalse(thread.is_alive())
        self.assertEqual(result.returncode, 0 if success else 1, result.stdout)
        self.assertEqual(dest.read_bytes(), body if success else b'previous complete file')
        self.assertFalse(pathlib.Path(str(dest) + '.part').exists())
        self.assertIn(('GET ' + path + ' HTTP/1.0\r\n').encode(), requests[0])

    def test_large_first_packet_and_long_url(self):
        body = bytes(range(256)) * 1024
        self.transfer(b'HTTP/1.1 200 OK\r\nContent-Length: 262144\r\n\r\n' + body, True, body, path='/' + '%20' * 240 + '.pkg')

    def test_fragmented_headers(self):
        self.transfer(b'HTTP/1.0 200 OK\r\ncontent-length: 4\r\n\r\nTEST', True, b'TEST', True)

    def test_truncated_body(self):
        self.transfer(b'HTTP/1.1 200 OK\r\nContent-Length: 100000\r\n\r\nshort', False)

    def test_http_errors(self):
        for status in (302, 401, 403, 404, 500):
            with self.subTest(status=status):
                self.transfer(f'HTTP/1.1 {status} Error\r\nContent-Length: 3\r\n\r\nbad'.encode(), False)

    def test_generated_zip_framing(self):
        buf = io.BytesIO()
        with zipfile.ZipFile(buf, 'w') as z:
            z.writestr('eboot.bin', b'ELF')
            z.writestr('sce_sys/param.json', b'{}')
        body = buf.getvalue()
        self.transfer(b'HTTP/1.0 200 OK\r\n\r\n' + body, True, body, generated=True)
        self.transfer(f'HTTP/1.1 200 OK\r\nContent-Length: {len(body)}\r\n\r\n'.encode() + body,
                      True, body, generated=True)
        chunks = b''.join(f'{len(part):x};test=1\r\n'.encode() + part + b'\r\n'
                          for part in (body[:41], body[41:]))
        header = b'HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n'
        self.transfer(header + chunks + b'0\r\nX-Test: yes\r\n\r\n',
                      True, body, fragmented=True, generated=True)
        for broken in (chunks, chunks + b'0\r\n', b'-1\r\n', b'zz\r\n', b'1\r\naXX'):
            self.transfer(header + broken, False, generated=True)
        self.transfer(b'HTTP/1.0 200 OK\r\n\r\n' + body[:-10], False, generated=True)
        self.transfer(b'HTTP/1.0 200 OK\r\n\r\n<html>Error</html>', False, generated=True)
        self.transfer(b'HTTP/1.1 200 OK\r\nContent-Length: 10\r\nTransfer-Encoding: chunked\r\n\r\n',
                      False, generated=True)

    def test_bad_framing(self):
        for headers in (b'', b'Content-Length: -1\r\n', b'Content-Length: 4\r\nContent-Length: 5\r\n', b'Transfer-Encoding: chunked\r\n', b'Content-Length: 4\r\nContent-Encoding: gzip\r\n', b'Content-Length: 4294967300\r\n'):
            with self.subTest(headers=headers):
                self.transfer(b'HTTP/1.1 200 OK\r\n' + headers + b'\r\nTEST', False)


if __name__ == '__main__':
    unittest.main()
