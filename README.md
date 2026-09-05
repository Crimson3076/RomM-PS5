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

`app/romm_ui` reads `/data/romm-ps5/config.txt` and serves the console browser at `http://127.0.0.1:8081/`. It lists RomM platforms and games. PS4 single-file `.pkg` downloads are saved in `/data/romm-ps5/downloads/`. The game page offers **Download only**, **Download and install with etaHEN**, **Install saved PKG with etaHEN**, and read-only package inspection. PS5 archive installation is not implemented.

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

### etaHEN installation

Enable **DPI v2** in etaHEN before requesting installation. RomM sends an
HTTP form POST to `127.0.0.1:12800/upload`, with the saved console path in `url`
and the filename in `content_name`. This matches
[etaHEN's DPI implementation](https://github.com/etaHEN/etaHEN/blob/main/Source%20Code/util/source/DirectPKGInstaller.cpp).
No RomM credentials or PKG bytes are sent to that endpoint. RomM no longer
calls Sony's installer directly, changes installer credentials, or serves `/pkg`.

**Download only** saves and checks the file without contacting etaHEN.
**Install saved PKG with etaHEN** reuses the saved file without downloading it;
it still needs RomM metadata access to identify the filename. Preflight checks
PS4 CNT magic, content ID and declared file size. **Inspect saved PKG** adds
bounded PlayGo structural checks. Neither inspection authenticates the package.

A `SUCCESS:` response means etaHEN accepted the request, not that installation
finished. Check PS5 notifications for completion. `FAILED:` responses preserve
etaHEN's error in the log. Missing DPI produces an enable-DPI message. A lost,
truncated, timed-out or unrecognized response is **unconfirmed**, because the
installation may already have started. Check etaHEN before retrying. RomM does
not retry or fall back to the old installer automatically. Saved files are retained.

One background worker handles transfers and submissions with a 512 KiB stack.
`/status` remains responsive and refreshes while the worker is active. Repeated
original download URLs return the latest result for that ROM and action;
explicit retry actions are provided. Closing the browser does not cancel a job.

Host tests cover the DPI wire protocol, escaped paths, acceptance versus failure,
missing services, incomplete replies, and download-only isolation. Console testing
is still required to verify the etaHEN handoff on the user's firmware.
