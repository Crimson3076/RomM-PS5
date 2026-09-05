"""Regression tests for the loopback PKG HTTP endpoint."""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class PkgHTTP(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.root = pathlib.Path(cls.tmp.name)
        src = cls.root / "harness.c"
        src.write_text(r'''
#define main payload_main
#include "app/romm_ui/main.c"
#undef main
int sceKernelSendNotificationRequest(int a, notify_request_t*b, size_t c, int d) { return 0; }
int sceAppInstUtilInitialize(void) { return 0; }
int sceAppInstUtilInstallByPackage(const pkg_metadata_t*a, pkg_info_t*b, playgo_info_t*c) { return 0; }
int main(int argc, char **argv) {
  int pair[2];
  char buf[4096];
  ssize_t n;
  snprintf(installer_pkg_path, sizeof installer_pkg_path, "%s", argv[1]);
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, pair)) return 2;
  serve_installer_pkg(pair[0], argv[2], atoi(argv[3]));
  while ((n = recv(pair[1], buf, sizeof buf, 0)) > 0)
    if (fwrite(buf, 1, (size_t)n, stdout) != (size_t)n) return 3;
  close(pair[1]);
  return 0;
}
''')
        cls.exe = cls.root / "pkg_http"
        subprocess.run(
            ["cc", "-Wall", "-Werror", "-pthread", "-I", str(ROOT),
             str(src), "-o", str(cls.exe)],
            check=True,
        )
        cls.pkg = cls.root / "fixture.pkg"
        cls.pkg.write_bytes(bytes(range(256)) * 4)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def request(self, headers="", head=False):
        result = subprocess.run(
            [str(self.exe), str(self.pkg), headers, "1" if head else "0"],
            capture_output=True,
            check=True,
        )
        return result.stdout

    def test_full_response(self):
        response = self.request()
        header, body = response.split(b"\r\n\r\n", 1)
        self.assertIn(b"HTTP/1.1 200 OK", header)
        self.assertIn(b"Content-Length: 1024", header)
        self.assertIn(b"Accept-Ranges: bytes", header)
        self.assertEqual(body, self.pkg.read_bytes())

    def test_bounded_range(self):
        response = self.request("Range: bytes=10-19\r\n\r\n")
        header, body = response.split(b"\r\n\r\n", 1)
        self.assertIn(b"HTTP/1.1 206 Partial Content", header)
        self.assertIn(b"Content-Range: bytes 10-19/1024", header)
        self.assertIn(b"Content-Length: 10", header)
        self.assertEqual(body, self.pkg.read_bytes()[10:20])

    def test_open_ended_range_and_head(self):
        response = self.request("range: bytes=1000-\r\n\r\n")
        header, body = response.split(b"\r\n\r\n", 1)
        self.assertIn(b"Content-Range: bytes 1000-1023/1024", header)
        self.assertEqual(body, self.pkg.read_bytes()[1000:])
        head = self.request("", head=True)
        self.assertTrue(head.endswith(b"\r\n\r\n"))

    def test_invalid_range(self):
        response = self.request("Range: bytes=2000-\r\n\r\n")
        self.assertIn(b"416 Range Not Satisfiable", response)
        self.assertIn(b"Content-Range: bytes */1024", response)


if __name__ == "__main__":
    unittest.main()
