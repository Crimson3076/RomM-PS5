# RomM-PS5

RomM-PS5 is an unofficial open-source homebrew client for browsing and downloading games from a self-hosted RomM server directly to a jailbroken PlayStation 5.

The project aims to provide a controller-friendly library browser, authenticated downloads through the RomM API, internal and external storage support, transfer validation, and compatibility with folder-based PS5 dumps and ShadowMountPlus image formats.

## Project Status

RomM-PS5 is currently in early development (Milestone 1: application
foundation) and is not ready for general use. Features, installation
procedures, storage layouts, and firmware compatibility may change.

There is no RomM network integration, no download, and no extraction code
yet — the current build renders a mock game list and proves the toolchain,
UI, and module structure work end to end. See `docs/testing.md` for exactly
what has and hasn't been verified, and `docs/architecture.md` for the
research this is being built on.

Do not install development builds unless you understand the risks involved. Interrupted or incorrect filesystem operations could result in incomplete files or data loss.

## Building

See [`docs/building.md`](docs/building.md) for host build instructions (the
only target actually built and tested so far) and the current, unverified
state of PS5 cross-compilation.

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

## Contributing

The project is currently establishing its architecture and build system. Development contributions, hardware testing, documentation, and security review will be welcome once the initial foundation is available.

## License

This project is licensed under the GNU General Public License v3.0. Third-party components remain subject to their respective licenses.
