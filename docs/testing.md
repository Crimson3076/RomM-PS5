# Testing status

This document tracks what has actually been built, run, and verified —
and, just as importantly, what has not — following the project rule that
AI-generated code is untrusted until reviewed, compiled, and tested, and
that untested functionality must never be described as working.

Status as of Milestone 1 (application foundation).

## What has been compiled and run

All of the following were built and executed in the development
environment (a Linux container with SDL2 installed via `libsdl2-dev`) as
part of this milestone — not just written:

| Area | What was verified |
|---|---|
| Host build | `cmake --build` succeeds from a clean `build/` dir, warning-free under `-Wall -Wextra -Wshadow -Wconversion -Wsign-conversion` |
| Unit tests | `ctest` and the direct test binary both pass: 115/115 checks across path validation, download state transitions, auth-header redaction, mock RomM API search/sort/pagination, storage discovery, and config load/save |
| Application smoke test | `./build/rommps5` runs under `SDL_VIDEODRIVER=dummy` for a bounded number of frames (`ROMM_PS5_SMOKE_TEST_FRAMES`) and exits 0 — proves SDL init, window/renderer creation (with software-renderer fallback, which is what actually engaged in this headless environment), the mock API load, the render loop, and clean shutdown all work without crashing |
| Controller absence handling | Confirmed via the same smoke test: with no game controller attached, `ui_input_init` logs and falls back to keyboard input rather than failing |

## What is mocked, not real

- **RomM connectivity**: `romm_api_mock_init()` is the only backend wired
  into the app. It returns a fixed, fictional 8-game fixture
  (`src/mockdata/mock_library.c`) — no network call happens anywhere in
  this milestone. A real HTTP-backed `RommApi` implementation is deferred
  until the network/TLS spike described in `docs/architecture.md` §1 is
  resolved.
- **HTTP transport**: `http_client_null_init()` is the only `HttpClient`
  implementation, and every call returns `HTTP_ERR_UNIMPLEMENTED`. It
  exists only so the interface shape can be written and compiled against
  now.
- **Downloads**: `DownloadProgress`/`download_progress_*` implement only
  the state machine and byte counters — no file is ever written, no bytes
  are ever transferred. There is no archive extractor yet.
- **Config**: persisted fields are limited to non-credential UI state
  (`selected_destination_index`, `fullscreen`). No RomM server URL or
  token is stored anywhere by this milestone's code, and a unit test
  (`test_config.c`) asserts the saved file never contains the strings
  `token`, `rmm_`, or `url`, specifically to catch a future regression
  here.

## What has NOT been tested at all

- **Anything on real PS5 hardware.** Nothing in this repository has been
  run on a console. This includes DualSense input via the PS5's actual
  `ps5-payload-dev/SDL` port (only desktop keyboard/`SDL_GameController`
  input on Linux has been exercised), PS5 firmware/filesystem behavior,
  and the `ps5-payload-dev/sdk` cross-compilation path itself (see
  `docs/building.md`).
- **PS5 cross-compilation.** `ROMM_PS5_TARGET_PLATFORM=ps5` has never been
  configured or built — the SDK was not available in this environment.
  CMake will refuse to configure that target at all without
  `PS5_PAYLOAD_SDK` set (see `CMakeLists.txt`), which is a guardrail, not
  a substitute for actually trying it.
- **Real RomM server integration.** No live RomM instance was reachable
  from this environment; the API endpoint/behavior findings in
  `docs/architecture.md` §3 come from reading RomM's own backend source,
  not from an end-to-end HTTP exchange. Authentication, real pagination,
  cover art, and both download code paths (live-stream and
  Range-triggered cache-build) are all still unverified against a running
  server.
- **Large files / large libraries.** The mock fixture has 8 entries and no
  file ever moves; nothing here says anything about behavior at the scale
  the task requires (100+ GB single files, large real libraries).
- **Any filesystem write into a real destination.** `storage_discover()`
  has only been tested against temp directories it created itself (see
  `tests/test_storage.c`) — never against the actual fixed candidate paths
  (`/data/etaHEN/games`, `/mnt/usb0/etaHEN/games`, etc.), which don't exist
  in this environment. No code in this milestone writes a game file
  anywhere.
- **GitHub Actions CI itself.** `.github/workflows/build.yml` was written
  to mirror exactly the commands verified locally (see above), but the
  workflow has not yet been observed running on GitHub's own runners as of
  this commit — confirm it goes green after this checkpoint is pushed
  before trusting it as a gate.

## Hardware test checklist (not yet run)

The full hardware test list lives in the top-level task description this
project was scoped from (valid/invalid URLs, expired/revoked tokens, USB
removal mid-transfer, etc.). None of it applies yet, because there is no
network or download code to exercise — it becomes relevant starting
Milestone 2 (RomM connection) and Milestone 3 (safe test download). This
file will grow a real pass/fail table once there's something on hardware
to test.
