"""Test PKG preflight and background-job behavior using the actual C code."""
import pathlib
import socket
import struct
import subprocess
import tempfile
import threading
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class Install(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.root = pathlib.Path(cls.tmp.name)
        src = cls.root / 'harness.c'
        src.write_text(r'''
#define main payload_main
#include "app/romm_ui/main.c"
#undef main
int sceKernelSendNotificationRequest(int a, notify_request_t*b, size_t c, int d) { return 0; }
int sceAppInstUtilInitialize(void) { return 0; }
int sceAppInstUtilInstallByPackage(const pkg_metadata_t*a, pkg_info_t*b, playgo_info_t*c) { return 0; }
int main(int argc, char **argv) {
  char id[49];
  if (!strcmp(argv[1], "inspect")) return inspect_pkg(argv[2], id) ? 1 : 0;
  config_t cfg = {0};
  int pair[2];
  char response[8192];
  strcpy(cfg.host, "127.0.0.1");
  snprintf(cfg.port, sizeof cfg.port, "%s", argv[2]);
  setvbuf(stdout, NULL, _IONBF, 0);
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, pair)) return 2;
  serve_download(pair[0], &cfg, "test", 7, 1, 0, 1, 0);
  ssize_t n = recv(pair[1], response, sizeof response - 1, 0);
  if (n <= 0) return 3;
  response[n] = 0;
  if (!strstr(response, "303 See Other") || !strstr(response, "Location: /status")) return 4;
  close(pair[1]);
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, pair)) return 5;
  serve_download(pair[0], &cfg, "test", 7, 1, 0, 1, 0);
  size_t used = 0;
  while ((n = recv(pair[1], response + used, sizeof response - used - 1, 0)) > 0) used += (size_t)n;
  response[used] = 0;
  close(pair[1]);
  if (!strstr(response, "Work continues") || !strstr(response, "url=/status")) return 6;
  puts("STATUS_OK");
  for (int i = 0; i < 500; i++) {
    pthread_mutex_lock(&transfer_lock);
    int active = transfer_job.active;
    pthread_mutex_unlock(&transfer_lock);
    if (!active) {
      if (socketpair(AF_UNIX, SOCK_STREAM, 0, pair)) return 7;
      serve_download(pair[0], &cfg, "test", 7, 1, 0, 1, 0);
      n = recv(pair[1], response, sizeof response - 1, 0);
      close(pair[1]);
      return n > 0 ? 0 : 8;
    }
    usleep(10000);
  }
  return 9;
}
''')
        cls.exe = cls.root / 'test'
        subprocess.run(['cc', '-Wall', '-Werror', '-pthread', '-I', str(ROOT), str(src), '-o', str(cls.exe)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def pkg(self, size=0x500):
        header = bytearray(0x500)
        header[:4] = b'\x7fCNT'
        content_id = b'UP0000-CUSA00001_00-TESTPACKAGE000001'
        header[0x40:0x40 + len(content_id)] = content_id
        struct.pack_into('>Q', header, 0x430, size)
        return header

    def inspect(self, data, success):
        path = self.root / 'fixture.pkg'
        path.write_bytes(data)
        result = subprocess.run([str(self.exe), 'inspect', str(path)], capture_output=True)
        self.assertEqual(result.returncode, 0 if success else 1, result.stdout)

    def test_valid_header(self):
        self.inspect(self.pkg(), True)

    def test_truncated_or_wrong_size(self):
        self.inspect(self.pkg()[:100], False)
        self.inspect(self.pkg(0x501), False)
        self.inspect(self.pkg(0x100000500), False)

    def test_wrong_body_and_content_id(self):
        self.inspect(b'<html>login</html>', False)
        header = self.pkg()
        header[0x40:0x70] = b'A' * 48
        self.inspect(header, False)
        header[0x40] = 10
        self.inspect(header, False)

    def test_status_responds_while_api_blocked_and_duplicate_does_not_restart(self):
        server = socket.socket()
        server.bind(('127.0.0.1', 0))
        server.listen(5)
        server.settimeout(5)
        release = threading.Event()
        requests = []
        errors = []
        def serve():
            try:
                with server:
                    for body in (b'[{"id":1,"slug":"ps4"}]', b'{"fs_name":"missing-test.pkg"}'):
                        conn, _ = server.accept()
                        with conn:
                            request = b''
                            while b'\r\n\r\n' not in request:
                                request += conn.recv(4096)
                            requests.append(request)
                            if not release.wait(3):
                                raise AssertionError('UI did not respond while API was blocked')
                            conn.sendall(b'HTTP/1.0 200 OK\r\nContent-Length: ' + str(len(body)).encode() + b'\r\n\r\n' + body)
            except Exception as exc:
                errors.append(exc)
        port = server.getsockname()[1]
        thread = threading.Thread(target=serve)
        thread.start()
        proc = subprocess.Popen([str(self.exe), 'async', str(port)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            # The harness can emit this only after both UI requests returned.
            line = proc.stdout.readline()
            self.assertEqual(line.strip(), 'STATUS_OK')
            release.set()
            output, error = proc.communicate(timeout=7)
            self.assertEqual(proc.returncode, 0, output + error)
        finally:
            release.set()
            if proc.poll() is None:
                proc.kill()
                proc.communicate()
            thread.join(timeout=6)
        self.assertFalse(thread.is_alive())
        self.assertFalse(errors, errors)
        self.assertEqual(len(requests), 2)
        self.assertIn(b'GET /api/platforms ', requests[0])
        self.assertIn(b'GET /api/roms/7 ', requests[1])


if __name__ == '__main__':
    unittest.main()
