# Changelog

All notable changes to this project are documented here. Format loosely
follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/). Versioning
will follow [Semantic Versioning](https://semver.org/) once tagged releases
begin (see the Release strategy in the project's planning notes) — everything
so far is unreleased `nightly` development.

## [Unreleased]

### First hardware test, and fixes in response

The PS5 cross-compilation milestone's artifact was run on real hardware for
the first time (console CFI-1215A Z2X, via `elfldr`) — see
`docs/testing.md` "First hardware test" for full results. It ran to
completion, the notification appeared, TCP logging worked, and
`storage_discover()` correctly found a real destination — but
`sceUserServiceGetInitialUser` failed, and the persistent log file was
missing several lines that appeared over TCP. Fixes below address both;
**neither has been hardware-verified yet** — see `docs/testing.md`.

- Investigated the pinned SDK directly (every sample, every `sce_stubs`
  file, the full `include/` tree) and confirmed
  `sceUserServiceGetInitialUser` is undocumented anywhere in it — no
  sample, header, or constant. The previous signature was sourced from
  general homebrew convention, not this SDK, and the hardware failure is
  real evidence it shouldn't be trusted. Per this project's policy against
  guessing undocumented ABI, `src/ps5/main_ps5.c` no longer calls it;
  `scePadOpen` (which needs a real user id) is skipped and logged as a
  documented blocker instead of an attempted-and-failed guess.
  `scePadInit()` (no arguments, nothing to guess) is still called.
- `sceUserServiceInitialize`/`sceNotificationSend`/`scePadInit` results are
  now always logged in both hex and signed decimal.
  `sceUserServiceTerminate` is now only called if initialize actually
  succeeded.
- Consolidated all application logging behind `src/log/log.c`: added an
  optional persistent-file sink (`log_init_file_sink`/
  `log_close_file_sink`) that every `log_message()` call fans out to
  automatically, flushed per line. Removed `main_ps5.c`'s hand-rolled
  duplicate-logging helper, which was the root cause of the missing lines
  — it required every call site to remember a second function call, and
  two didn't. Added `tests/test_log_file_sink.c` (20 checks: fan-out,
  no duplication, failed-sink nonfatality, pre-close flush durability).
- Applied `.clang-format` to every file touched this round.

### PS5 cross-compilation milestone

- Bootstrapped `ps5-payload-dev/sdk`, pinned to git tag `v0.43`, built from
  source with `clang-18`/`lld-18` — not the project's `main` branch or its
  prebuilt release zip, to keep the toolchain reproducible and free of
  unexplained binaries.
- Fixed a real CMake toolchain bug: the PS5 toolchain must be supplied via
  `-DCMAKE_TOOLCHAIN_FILE=...` on the configure command line, not
  `include()`d from inside `CMakeLists.txt` — the latter silently kept
  using the host compiler. Target detection now checks `PROSPERO` (set by
  the SDK's own `toolchain/prospero.cmake`) instead of a hand-rolled cache
  variable.
- Added `src/ps5/main_ps5.c`, a PS5-native entry point (no SDL) that
  initializes UserService, logs via this project's own `log` module, reads
  the console's hardware model name, runs `storage_discover()` against the
  real fixed destination list, sends an on-screen notification toast, and
  opens (but does not read from) a real `ScePad` controller handle.
- Confirmed `rommps5_core` (`pathval`, `download`, `log`, `config`,
  `storage`, `net`'s null client, `romm_api_mock`, `mockdata`) compiles
  **unchanged** for the PS5 target — same source, same CMake target.
- Corrected the Milestone 0 assumption that `ps5-payload-dev/SDL` was a
  ready rendering path: the pinned SDK's own sample set has no SDL example
  and doesn't reference that port at all. SDL stays strictly host-only;
  the PS5 target uses the SDK's own real approach (`SceNotification`,
  `ScePad`, `SceUserService`) instead. See `docs/architecture.md` §5a.
- Added a `ps5-cross-compile` CI job that bootstraps the pinned SDK and
  builds `build-ps5/rommps5-ps5`, uploaded as a workflow artifact (not a
  release).
- Updated `docs/building.md` and `docs/testing.md` with exact, verified
  commands and an explicit list of what still requires real PS5 hardware.

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
