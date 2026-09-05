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
