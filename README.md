# RomM-PS5

RomM-PS5 is an unofficial open-source homebrew client for browsing and downloading games from a self-hosted RomM server directly to a jailbroken PlayStation 5.

The project aims to provide a controller-friendly library browser, authenticated downloads through the RomM API, internal and external storage support, transfer validation, and compatibility with folder-based PS5 dumps and ShadowMountPlus image formats.

## Project Status

RomM-PS5 is currently in early development and is not ready for general use. Features, installation procedures, storage layouts, and firmware compatibility may change.

Do not install development builds unless you understand the risks involved. Interrupted or incorrect filesystem operations could result in incomplete files or data loss.

## Planned Features

* RomM Client API Token authentication
* PS5 library browsing and search
* Cover artwork and game metadata
* Internal and external storage selection
* Download progress, cancellation, and retry handling
* Safe temporary files and transfer validation
* Extracted folder dump support
* `.ffpkg`, `.exfat`, `.ffpfs`, and `.ffpfsc` downloads
* ShadowMountPlus-compatible destination paths
* DualSense-friendly interface
* Sanitized diagnostic logs

## Legal Notice

RomM-PS5 does not provide games, firmware, encryption keys, backports, exploits, or copyrighted Sony files.

Users are responsible for supplying their own legally obtained game backups and for complying with applicable laws. This project is not affiliated with or endorsed by RomM, Sony Interactive Entertainment, PlayStation, or LightningMods.

## Security

Never commit RomM passwords or Client API Tokens to this repository. Credentials must be entered locally and excluded from logs, screenshots, examples, and test fixtures.

## Building the App

`app/romm_client` is a first milestone: it connects to a RomM server over plain HTTP (not HTTPS -- TLS is not yet implemented), authenticates with a username/password over HTTP Basic auth, fetches `GET /api/roms`, and shows the total game count as a PS5 notification.

Build it with the [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk):

```
export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk
make -C app/romm_client
```

Before running it, copy `app/romm_client/config.example.txt` to `app/romm_client/config.txt` and fill in your RomM server's LAN host/port and credentials, then place it on the PS5 at `/data/romm-ps5/config.txt`. `config.txt` is gitignored so credentials are never committed.

## On-Screen UI

`app/romm_ui` is the first on-screen browsing milestone. It reads the same `/data/romm-ps5/config.txt` and runs a tiny local HTTP server on the console at `127.0.0.1:8081`. `sceSystemServiceLaunchWebBrowser` was tried for auto-launching the PS5's system browser, but it appears to only work reliably from installed PKG apps, not raw elfldr payloads -- so instead the payload shows a PS5 notification with the URL, and you open the system Browser app yourself (installing the community `internetbrowser-ps5.pkg` first, if Debug Settings -> Web errors out) and navigate to it. The landing page looks up the PS4 and PS5 platform IDs from `GET /api/platforms` (by matching `slug`) and offers a choice between them; picking one lists that platform's ROMs from `GET /api/roms?platform_ids=...`, with Prev/Next links for pagination. Clicking a game opens a details page (name, platform, file size, from `GET /api/roms/{id}`) with a Download link. For PS4 titles (stored on RomM as `.pkg` files), Download streams the file from `GET /api/roms/{id}/content/{file_name}` straight to `/data/romm-ps5/downloads/` on the console (no in-memory buffering, since these can be multi-gigabyte), then triggers installation via the native `sceAppInstUtilInstallByPackage` API (struct layouts and usage taken from [ps5-payload-dev/shsrv](https://github.com/ps5-payload-dev/shsrv)'s own package installer, not etaHEN's DPI, since etaHEN itself is being retired in favor of a bare `elfldr`+`kstuff` setup). PS5 titles are stored on RomM as `.zip` archives of ShadowMountPlus-ready folder dumps; extracting those on-console would require implementing DEFLATE decompression from scratch (no linkable userland zlib exists in this SDK), so that's deferred as a separate, harder milestone -- picking a PS5 title currently shows an honest "not implemented for this platform yet" page instead. DualSense navigation (D-pad/X/Circle) comes from the system browser's own built-in controller-to-page input mapping -- no JavaScript gamepad API is used.

Build and deploy it the same way as `romm_client`:

```
make -C app/romm_ui
export PS5_HOST=<PS5_IP> PS5_PORT=9021
make -C app/romm_ui test
```

## Development Deployment

Once a build produces a compiled ELF payload, `tools/deploy_payload.py` sends it to a jailbroken PS5 running [etaHEN](https://github.com/etaHEN)'s `elfldr` listener (default port 9021) over a plain TCP connection, then prints any stdout/stderr the payload streams back:

```
python3 tools/deploy_payload.py --host <PS5_IP> path/to/payload.elf
```

This script assumes the console is already jailbroken by the user and `elfldr` is already running; it does not perform a jailbreak or exploit of any kind.

## Contributing

The project is currently establishing its architecture and build system. Development contributions, hardware testing, documentation, and security review will be welcome once the initial foundation is available.

## License

This project is licensed under the GNU General Public License v3.0. Third-party components remain subject to their respective licenses.

## Download diagnostics and regression tests

PS4 downloads require a single `.pkg` entry and a plain HTTP endpoint returning
HTTP 200 with a nonzero `Content-Length`. The downloader requests HTTP/1.0 and
identity encoding. Redirects, chunked responses, and compressed responses fail
explicitly; HTTPS and redirect handling are not implemented.

Transfers are written to `.part` files, checked against the declared byte count,
and renamed only after successful writes and close. This checks transfer
completeness, not package authenticity or install compatibility. A failed
transfer preserves an existing completed file. Socket reads/writes time out
after 60 seconds of inactivity. Downloads run in a background worker; the browser redirects immediately to a read-only status page.

Capture payload stdout with `tools/deploy_payload.py`. Failure lines include
HTTP status and received/expected bytes, without authentication headers. A
successful download followed by an installation error retains the PKG and shows
the installer error code in the browser.

Run host-side downloader regression tests (requires a C compiler and Python):

```
python3 -m unittest discover -s tests -v
```

Console validation still requires building with the PS5 payload SDK and testing
against a real RomM server and PS5.

### Installer retry revision

The PlayGo output buffer now matches the 9,984-byte layout in the
[etaHEN package installation writeup](https://github.com/etaHEN/etaHEN/blob/main/PS5%20technical%20writeups/pkg-writeup.md).
The previous 6,976-byte buffer was undersized. Compile-time assertions check
its size and field offsets. The package-info argument remains a zeroed output
structure; an empty value in the native log alone does not diagnose a failure.
The package content ID is read from the header and supplied as input metadata.
The exact cause of console error `0x80b2116f` remains unconfirmed pending retest.

On a game page, **Install saved PKG (no download)** reuses the existing file in
`/data/romm-ps5/downloads/`. It never requests the content endpoint. It requires
RomM metadata access to identify the saved filename. A preflight checks PS4 CNT
magic, a printable content ID, and file size against the big-endian package-size
field documented by [LibOrbisPkg](https://github.com/maxton/LibOrbisPkg/blob/master/LibOrbisPkg/PKG/PkgReader.cs).
This does not verify signatures or every package entry. A failed preflight
retains the file and does not call the installer.

Download and installation work runs in one background worker. `/status` displays
byte counts and refreshes every three seconds while active. Progress also prints
to stdout about every five seconds during transfer. Repeated original Download
URLs return the active/latest result for that ROM rather than restarting it;
explicit retry actions are provided. Closing or refreshing the browser does not
cancel a job. Installation accepted means the native request returned success,
not that asynchronous installation has completed; check PS5 notifications.
