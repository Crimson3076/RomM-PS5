# Testing status

This document tracks what has actually been built, run, and verified —
and, just as importantly, what has not — following the project rule that
AI-generated code is untrusted until reviewed, compiled, and tested, and
that untested functionality must never be described as working.

Status as of the PS5 cross-compilation milestone (follows Milestone 1,
application foundation).

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
| PS5 SDK bootstrap | `ps5-payload-dev/sdk` git tag `v0.43` built from source with `clang-18`/`lld-18` and `make DESTDIR=/opt/ps5-payload-sdk install` completed with no errors (a handful of pre-existing warnings inside the SDK's own `libc`/`libufs` code, not this project's) |
| PS5 SDK toolchain sample | The SDK's own `samples/hello_world` built against that install and produced a valid `ELF 64-bit LSB pie executable, x86-64` |
| `rommps5_core` for PS5 | Compiles **unchanged** (same source, same CMake target) against the pinned SDK: `pathval`, `download`, `log`, `config`, `storage`, `net`'s null client, `romm_api_mock`, `mockdata` — see `docs/building.md` |
| PS5 ELF build | `src/ps5/main_ps5.c` compiles and links against the pinned SDK, `-Wall -Wextra -Wshadow -Wconversion -Wsign-conversion` warning-clean, producing a real PS5 ELF with correct `NEEDED .sprx` entries for `libSceUserService`, `libSceNotification`, `libkernel_sys`, `libScePad`, `libSceLibcInternal`, `libSceNet` — reproducible: two independent clean builds produced byte-identical SHA-256 output |

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
  run on a console — this is still true after this milestone, which only
  established that a real PS5 ELF can be *built*. Specifically unverified:
  - Whether `src/ps5/main_ps5.c` actually runs to completion instead of
    crashing, hanging, or being rejected by elfldr.
  - Whether the notification toast (`sceNotificationSend`) actually
    appears on screen.
  - Whether `/data/romm-ps5/ps5-hello.log` is actually writable at the
    point this payload runs, and whether stdout/stderr actually relay
    back over the elfldr TCP connection the way `docs/building.md`
    describes (that description is based on how `ps5-payload-dev/sdk`'s
    own `samples/hello_stdio` is documented to behave, not on having
    observed it here).
  - Whether `scePadOpen`'s returned handle is actually valid — its call
    signature (`scePadInit`/`scePadOpen`/`scePadClose`) follows the
    widely-published PS4/PS5 homebrew convention, but is not documented by
    this SDK and has not been confirmed against real firmware.
  - `scePadReadState` (actual button/stick state) is not called at all —
    deliberately deferred; see `docs/building.md` "Input mapping".
  - DualSense input via the host-only `SDL_GameController` path is
    irrelevant to the PS5 target (it's host-build-only, see
    `docs/building.md`) and remains desktop-only-tested regardless.
  - PS5 firmware version/compatibility beyond "targets 10.60" per the
    project's stated scope — nothing here has run against any firmware.
  - `statvfs`/`access`/`stat`/`mkdir`/`fopen` behavior specifically on PS5:
    these compiled fine (resolved against `libSceLibcInternal.sprx`'s
    stub at link time — see `docs/building.md`), but the pinned SDK's own
    `libc.a` does not implement them itself, so their actual runtime
    behavior on the console is unverified. This affects `storage_discover`
    (used directly in `src/ps5/main_ps5.c`) and the log-file write in the
    same file.
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
- **GitHub Actions CI itself.** `.github/workflows/build.yml` now has two
  jobs, `host-build-test` and `ps5-cross-compile`; both were written to
  mirror exactly the commands verified locally (see above). See this
  milestone's session report for the actual observed run status — do not
  trust either job as a gate until you've confirmed its current status
  yourself, since a workflow file can drift from what actually ran.

## Hardware test checklist (not yet run)

The full hardware test list lives in the top-level task description this
project was scoped from (valid/invalid URLs, expired/revoked tokens, USB
removal mid-transfer, etc.). Most of it still doesn't apply, because there
is no network or download code to exercise — that becomes relevant starting
Milestone 2 (RomM connection) and Milestone 3 (safe test download).

One item is now actionable and should be the very next hardware test done
on this project, ahead of any of the above: **deploy
`build-ps5/rommps5-ps5` to a real jailbroken PS5 via
`socat -t 9999999 - TCP:<PS5_IP>:9021 < build-ps5/rommps5-ps5`** (see
`docs/building.md`) and confirm, in order:
1. The transfer completes and the loader accepts the ELF.
2. It runs without crashing the loader or the console.
3. A "RomM-PS5" toast notification appears on screen.
4. Log lines appear in the terminal running the deploy command.
5. `/data/romm-ps5/ps5-hello.log` exists on the console afterward and
   contains the same lines.
6. `scePadOpen` reports a plausible (non-negative) handle when a DualSense
   is connected.
7. The process actually exits — the loader is ready to accept another
   payload afterward, and the console is left in a normal state.

Record the result of each step (and the console's firmware version)
here once it's been tried, rather than in the running work-session report
alone, so this file stays the single source of truth for what's actually
been confirmed on hardware.
