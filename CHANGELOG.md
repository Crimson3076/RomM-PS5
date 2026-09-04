# Changelog

All notable changes to this project are documented here. Format loosely
follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/). Versioning
will follow [Semantic Versioning](https://semver.org/) once tagged releases
begin (see the Release strategy in the project's planning notes) — everything
so far is unreleased `nightly` development.

## [Unreleased]

### Milestone 1 — Application foundation

- Added a CMake-based project structure separating the codebase into
  distinct modules: `ui`, `romm_api`, `net`, `storage`, `pathval`,
  `download`, `config`, `log`, and `mockdata`.
- Added a mock `RommApi` backend (`romm_api_mock`) serving a fixed,
  fictional 8-game PS5 library fixture, with case-insensitive search,
  ascending/descending title sort, and offset/limit pagination.
- Added a minimal SDL2 UI: window/renderer lifecycle, a placeholder-bar
  game list (real glyph text rendering is not yet implemented — see
  `docs/testing.md`), and DualSense/keyboard-driven focus navigation via
  `SDL_GameController`.
- Added a pure, unit-tested path-safety module (`pathval`) rejecting
  absolute entry paths, `..` traversal (including buried mid-path), and
  malformed segments, ahead of any real archive extraction code.
- Added a pure, unit-tested download state machine (`download_manager`)
  covering the DOWNLOADING/EXTRACTING/VALIDATING/COMPLETED/FAILED/
  CANCELLED lifecycle — no real transfer or extraction logic yet.
- Added a storage-discovery module implementing the project's fixed
  candidate destination list, with an injectable path prefix for
  desktop testing.
- Added mandatory log redaction for RomM Client API Tokens and
  `Authorization` header values (`log_redact_bearer_token`,
  `log_redact_auth_header`), unit-tested to confirm a token's middle
  section never appears in redacted output.
- Added a non-credential config module (persists only UI state; a unit
  test asserts the saved file never contains `token`, `rmm_`, or `url`).
- Added placeholder `HttpClient`/`RommApi` interface shapes so later
  milestones can implement a real backend without changing call sites —
  no implementation performs a real network request yet
  (`http_client_null` always returns "unimplemented").
- Added a host-only unit test suite (115 checks, 0 dependencies beyond the
  core module library) and a GitHub Actions workflow building and testing
  the host target on every push/PR to `nightly`/`main`.
- Added `docs/building.md` and `docs/testing.md`.

### Milestone 0 — Research

- Selected `ps5-payload-dev/sdk` (successor to the archived
  `john-tornblom/ps5-payload-sdk`) as the PS5 homebrew toolchain, and
  `ps5-payload-dev/SDL` for controller input/rendering.
- Documented RomM's REST API (auth, platform discovery, ROM listing/
  search/pagination, single-file and folder-based download behavior)
  first from external docs, then corrected and expanded against RomM's
  own backend source (`rommapp/romm`).
- Established that RomM's folder-download resume support depends on a
  server-side ZIP cache build, not a property of the live stream — a
  significant correction to the initial external-docs-based assumption.
  See `docs/architecture.md` §3–4.
- Added `docs/architecture.md`, `.gitignore`.

[Unreleased]: https://github.com/Crimson3076/RomM-PS5/compare/main...nightly
