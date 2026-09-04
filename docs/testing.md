# Testing status

This document tracks what has actually been built, run, and verified —
and, just as importantly, what has not — following the project rule that
AI-generated code is untrusted until reviewed, compiled, and tested, and
that untested functionality must never be described as working.

Status as of the fourth hardware test. The direct-memory allocation fix is
hardware-confirmed: VideoOut opened, the exact 17 MiB allocation and mapping
succeeded, and both scanout buffers registered. The run then reached real
ScePad and HTTPS requests. ScePad rejected the reported login user with
`0x809b0081` (`USER_NOT_LOGIN`), while `sceHttp2SendRequest` failed before an
HTTP status was received. The tested build did not preserve either the raw
SceHttp2 result or `errno`; the follow-up diagnostic build does. See below for
the exact verified boundary.

## Fourth hardware test - RESULTS (video fixed, pad and HTTPS reached)

Console: **CFI-1215A Z2X**. Delivery: `elfldr` over TCP port 9021. Artifact:
the canonical CI-built `rommps5-ps5` from GitHub Actions run
[`33913388827`](https://github.com/Crimson3076/RomM-PS5/actions/runs/33913388827)
(commit `dfd4174`), SHA-256
`c63b412dc9460958a646d7597a538c9ada4c793a87fa3ebda4b269e790ddbe52`.

### Confirmed on physical hardware

- The 17 MiB two-buffer direct-memory request succeeded. Mapping, equeue
  creation, flip-event registration, flip rate, and
  `sceVideoOutRegisterBuffers2` all succeeded. `video_init()` completed at
  1920x1080. This confirms the allocation fix and the complete initialization
  path on this console.
- The app presented its configuration and connection screens without a logged
  flip or wait failure. The operator has not separately confirmed the visible
  pixel output, so this is an execution-path observation, not a claim that the
  UI looked correct.
- `sceUserServiceGetLoginUserIdList` returned success and one candidate user,
  `0x15bf8af5` (`364874485`). `scePadOpen` rejected it with `0x809b0081`, the
  public Orbis Device Service `USER_NOT_LOGIN` error. No controller handle was
  obtained and `scePadReadState` was not reached.
- The real config loaded from `/data/homebrew/RomM-PS5/config.json` and the
  SceNet/SceSsl/SceHttp2 startup sequence completed. The application reached a
  real request to the configured RomM `/api/platforms` endpoint.
- `sceHttp2SendRequest` failed before any HTTP response status was available.
  The tested implementation logged only a generic failure and discarded the
  SCE result and `errno`, so this run cannot distinguish DNS, TCP, TLS,
  certificate, or HTTP2-layer failure.
- `sceSystemServiceHideSplashScreen` returned `0x80940004` and caused repeated
  `SceLncUtil` warnings. VideoOut still initialized successfully, proving that
  this LNC-only call is unnecessary for an `elfldr` payload.

### Fixes prepared for the next hardware test

- Removed `sceSystemServiceHideSplashScreen` and the now-unused
  `SceSystemService` link dependency.
- Pad startup now logs all four login slots, validates each nonnegative ID
  through `sceUserServiceGetUserName` without logging the account name, tries
  every candidate, checks `scePadGetHandle` after failed opens, and finally
  tries `PAD_USER_ID_SYSTEM` once. It labels `0x809b0081` explicitly.
- Every SceNet/SceSsl/SceHttp2 boundary now logs the full result in hexadecimal
  and signed decimal plus `errno`. Authorization-header diagnostics never log
  the header value or token.
- If HTTPS still fails, test a direct LAN `http://` RomM URL next. The pinned
  SDK's only HTTP2 sample demonstrates plain HTTP, not HTTPS, so that test will
  separate basic networking from TLS without weakening TLS verification in the
  app.

## Third hardware test - RESULTS (vertical-slice UI attempt)

Console: **CFI-1215A Z2X**. Delivery: `elfldr` over TCP port 9021. Artifact:
the canonical CI-built `rommps5-ps5` from GitHub Actions run
[`33911597953`](https://github.com/Crimson3076/RomM-PS5/actions/runs/33911597953)
(commit `1344b15`), SHA-256
`4006d50e4d279e3b30b06f017012b790eeb64333889118fb8a6a90b3ea36f01a`.

### Confirmed on physical hardware

- The vertical-slice ELF was accepted by `elfldr`, started, initialized
  UserService, detected the hardware model, and exited cleanly after the video
  error.
- `sceVideoOutOpen` returned a non-negative handle because execution advanced
  to the following direct-memory allocation.
- The fixed `0x4000000` (64 MiB) `sceKernelAllocateMainDirectMemory` request
  failed with `errno` reported as `Resource temporarily unavailable`.
- No UI appeared. Configuration, networking, RomM access, and real controller
  input were not reached, so this test provides no evidence about them.

### Fix prepared in response, now hardware-verified

- The two scanout buffers now use an overflow-checked, alignment-aware layout
  calculated from `tilemap_buffer_size()`. At 1920x1080 this requests
  `0x880000` bytes per buffer and `0x1100000` bytes total, approximately 17
  MiB, instead of the reference backend's fixed 64 MiB reservation.
- Every video initialization boundary now logs its SCE return value in
  hexadecimal and decimal plus `errno` where applicable. Allocation logs also
  include size, alignment, and memory type.
- Host tests assert the exact 1080p and 720p tiled-buffer layouts and reject
  invalid or overflowing calculations.
- The fourth hardware test confirmed the full initialization sequence succeeds
  with this exact allocation layout.

## First hardware test — RESULTS (real console)

**This is the first time any part of this project ran on real PS5
hardware.** Console: **CFI-1215A Z2X**. Delivery: `elfldr` over TCP port
9021. Artifact tested: the *locally-built* `rommps5-ps5`,
**SHA-256 `4e72c4c63b978012551e4b197b1017017ffce13d6bac6b6bb0b5d1e1186ce7ba`**
(built before the fixes in this section existed — see "Fixes applied"
below for what changed since).

### Confirmed working on physical hardware

- The artifact was accepted by `elfldr` and ran to completion without
  crashing the loader or the console.
- `sceUserServiceInitialize(0)` succeeded (no failure logged for it).
- `sceKernelGetHwModelName` returned the real model string, `CFI-1215A Z2X`.
- `storage_discover()` correctly found `/data/etaHEN/games` (1 of the 18
  fixed candidate paths) as existing and writable, and reported real free
  space (`515746824192` bytes) via `statvfs` — the first real confirmation
  that `storage.c` behaves correctly on PS5, not just that it compiles.
- `sceNotificationSend` succeeded and the toast notification appeared on
  screen — confirmed by the operator, not just by the return code.
- Log lines were visible live over the `elfldr` TCP connection — confirms
  the stdout/stderr-relay behavior described in `docs/building.md` is real
  on this console/loader, not just documented-by-inference.
- A persistent log file was created at `/data/romm-ps5/ps5-hello.log` and
  was retrievable over FTP — confirms that path is writable and the file
  survives after the payload exits.
- The process exited and returned control to the loader normally (safe
  exit confirmed).

### Confirmed failing / incomplete on physical hardware

- **`sceUserServiceGetInitialUser` failed** (logged as a warning; exact
  numeric code not captured, since the code at the time only logged
  pass/fail, not the value — fixed in this milestone, see below). As a
  direct consequence, `scePadOpen`/`scePadClose` were never reached at
  all in this test — **no ScePad call beyond `scePadInit` even ran**, so
  nothing about ScePad's real behavior was learned from this test.
- **The persistent log file was incomplete.** Observed TCP output had 6
  lines; the retrieved `/data/romm-ps5/ps5-hello.log` had only 4 — missing
  both the `storage_discover` lines and the `sceUserServiceGetInitialUser`
  warning. Root cause (confirmed by code inspection, not guessed): the
  code at the time only fanned a message out to both stdout/stderr *and*
  the file when a call site remembered to call a second, manual
  `logfile_line()` helper — the storage-discovery loop and that specific
  warning used `log_info()`/`log_warn()` alone, which only ever wrote to
  stderr. Fixed in this milestone by consolidating all logging behind one
  API that fans out automatically — see "Fixes applied" below.

### Exact observed TCP output (this test)

```text
[2026-09-04T18:31:48Z] INFO  RomM-PS5 PS5-target starting (cross-compilation milestone)
[2026-09-04T18:31:48Z] INFO  Hardware model: CFI-1215A Z2X
[2026-09-04T18:31:48Z] INFO  storage_discover: 1 of 18 candidate destinations exist and are writable
[2026-09-04T18:31:48Z] INFO    destination: /data/etaHEN/games (515746824192 bytes free)
[2026-09-04T18:31:48Z] INFO  Notification toast sent
[2026-09-04T18:31:48Z] WARN  sceUserServiceGetInitialUser failed; skipping ScePad
[2026-09-04T18:31:48Z] INFO  RomM-PS5 PS5-target exiting cleanly
```

### Exact retrieved persistent log (this test, `/data/romm-ps5/ps5-hello.log` over FTP)

```text
RomM-PS5 PS5-target starting (cross-compilation milestone)
Hardware model: CFI-1215A Z2X
Notification toast sent
RomM-PS5 PS5-target exiting cleanly
```

### Still unverified after this test

Real DualSense/`ScePad` behavior — nothing about it was learned, since
`scePadOpen` was never reached. See "Fixes applied" for what changed and
what is still a documented, unresolved blocker rather than a fix.

## Second hardware test — RESULTS (retest, logging fix confirmed)

**Retest of the fixes below, on the same console.** Artifact: the
*canonical CI-built* `rommps5-ps5` from GitHub Actions run
[`33905275501`](https://github.com/Crimson3076/RomM-PS5/actions/runs/33905275501)
(commit `69bc48c`), **SHA-256
`9f5ea26cba4f6d4818158d774d7fd8fa59a6a1368094e17af82492368bacf8e9`**.
Console: **CFI-1215A Z2X** (same unit as the first test). Delivery:
`elfldr` over TCP port 9021.

### Confirmed working on physical hardware (this retest)

- The payload executed and exited cleanly, same as the first test.
- `sceUserServiceInitialize` returned `0x0 (0)` — confirms the new
  always-log-the-value-in-hex-and-decimal behavior works as designed on
  real hardware, and that UserService init still succeeds on this console.
- Hardware model detection succeeded (same as the first test).
- `storage_discover` again found `/data/etaHEN/games` writable with
  `515746824192` bytes free — identical to the first test, as expected
  (same console, same storage state).
- `sceNotificationSend` returned `0x0 (0)`.
- `scePadInit` returned `0x0 (0)` — the first real data point on this
  call's behavior on this console (the first test never reached it,
  since the old code gated `scePadInit` behind the since-removed
  `sceUserServiceGetInitialUser` call). This says `scePadInit()` itself
  does not fail on this console; it says nothing about `scePadOpen` or
  `scePadReadState`, which are still not called.
- `scePadOpen` was skipped, logged exactly as
  `"scePadOpen skipped: no SDK-verified way to obtain a real user id..."`
  — **this confirms the documented-blocker behavior works exactly as
  intended**: the code correctly recognizes it has no verified user id
  and skips the guessed call, rather than either crashing or silently
  attempting it.
- TCP output contained the complete expected run (all lines from the
  "Retest checklist" below were present and in order).
- **`/data/romm-ps5/ps5-hello.log` now contains the complete timestamped
  run, including every line that previously appeared over TCP but not in
  the file (the `storage_discover` lines and the ScePad-skip warning).**
  This is the specific regression the logging fan-out fix targeted, and
  it is now **confirmed fixed on real hardware**, not just by host-side
  tests using synthetic messages.

**Persistent-log append behavior, confirmed as designed, not a bug**: the
retrieved log file starts with the four un-timestamped lines left over
from the first test (before the fan-out fix existed, and from a version
of the code that didn't yet prefix lines with a timestamp), followed by
the complete, fully-timestamped second run appended after them.
`log_init_file_sink()` opens the file in append (`"a"`) mode by design —
a durable diagnostic log is expected to accumulate across runs of the
payload rather than being truncated each time, which is exactly what a
"crash log location" needs to be useful after the fact. Anyone comparing
raw byte counts between TCP output and the file should account for this:
the file legitimately contains more than just the most recent run.

### Still not verified (unchanged — do not treat as confirmed)

- **Real controller input.** `scePadOpen` was correctly *skipped*, not
  *exercised* — nothing about whether it would succeed, what a real user
  id would need to look like, or `scePadReadState`'s behavior has been
  learned. This remains a documented, unresolved blocker, not a working
  feature.
- Everything else already listed under "What has NOT been tested at all"
  below that this retest didn't touch (real RomM integration, large
  files, other consoles/firmware, etc.).

## Fixes applied in response to the first hardware test — HARDWARE-VERIFIED

Everything in this section was compiled, cross-compiled for PS5, and
covered by new host-side tests, and has now also been **confirmed on real
hardware** by the second hardware test above (console CFI-1215A Z2X,
artifact SHA-256 `9f5ea26c...b1ffc5f9`). The one exception, called out
inline below, is real `ScePad` controller input, which remains an
unresolved, documented blocker — do not treat it as verified.

- **`sceUserServiceGetInitialUser` investigation.** Directly inspected the
  pinned SDK (`ps5-payload-dev/sdk` v0.43): every sample's source, every
  stub file under `sce_stubs/`, and the full `include/` tree. Result: this
  function is a real exported symbol
  (`sce_stubs/libSceUserService.so`/`.c`), but no sample calls it, no
  header declares it, and no SCE_USER_SERVICE_ERROR-style constants exist
  anywhere in this SDK to interpret its return value against. The
  `int32_t *userId` signature previously used in `src/ps5/main_ps5.c` was
  sourced from general PS4/PS5 homebrew convention, not from this SDK —
  the hardware failure is real evidence that guess shouldn't be trusted
  further. Per this project's policy against guessing undocumented ABI,
  **this function is no longer called at all.** This is a documented
  blocker, not a fix: there is currently no SDK-verified way, anywhere in
  the pinned SDK, to obtain a real user id from a raw `elfldr`-launched
  payload. See the file-level comment in `src/ps5/main_ps5.c` for the full
  investigation trail.
- **Consequence for ScePad** — **the skip behavior is hardware-confirmed;
  real ScePad input is NOT**: `scePadOpen`/`scePadClose` are only called
  when a verified user id is available — which, per the above, never
  happens right now, so they are skipped and logged as a documented
  blocker (`"scePadOpen skipped: no SDK-verified way to obtain a real
  user id..."`) rather than attempted with a guessed value. The second
  hardware test confirmed this exact log line appears and `scePadOpen` is
  correctly never reached — but that only verifies the *skip logic*, not
  controller input itself, which remains entirely untested.
  `scePadInit()` (which takes no arguments, so there is nothing about its
  call shape to guess) is still called unconditionally; its return value
  is now logged in both hex and signed decimal, and the second hardware
  test observed `0x0 (0)` — the first real data point on this call, though
  it says nothing about `scePadOpen`/`scePadReadState`.
- **`sceUserServiceInitialize`/`sceNotificationSend`/`scePadInit` return
  values are now all logged in both hex and signed decimal** (previously
  only pass/fail was logged for some of them), e.g.
  `sceUserServiceInitialize returned 0x0 (0)` — **confirmed on hardware**
  in the second test (all three logged `0x0 (0)`).
- **`sceUserServiceTerminate` is now only called if `sceUserServiceInitialize`
  actually returned 0** — previously it was called unconditionally. Not
  independently observable from the TCP/log output (it produces no log
  line of its own), so this remains verified by code review and host
  compilation only, not by a distinguishing hardware observation.
- **Logging consolidated behind one API — confirmed fixed on hardware.**
  `src/log/log.c` now owns an optional persistent-file sink
  (`log_init_file_sink()`/`log_close_file_sink()`); every `log_message()`
  call (i.e. every `log_info`/`log_warn`/`log_error`/`log_debug` in the
  whole codebase) formats its line once and writes the identical bytes to
  both stderr and the file sink (if open), flushing the file immediately
  on every line — not buffered until shutdown. The second hardware test
  confirmed `/data/romm-ps5/ps5-hello.log` now contains every line that
  appears over TCP, which is the specific defect this fixed.
  `src/ps5/main_ps5.c`'s previous hand-rolled `logfile_line()`
  duplicate-logging helper was deleted entirely; nothing in this
  project's own code calls `printf`/`fprintf` for application
  diagnostics anymore (`config.c`'s file writes are config serialization,
  a different concern, and are unaffected). New host-side tests
  (`tests/test_log_file_sink.c`, 20 checks) cover: INFO/WARN/ERROR all
  reaching the file, storage-discovery- and failing-service-shaped
  messages reaching the file, no duplication, a failed file-sink open
  being nonfatal (and not corrupting subsequent logging), and content
  being flushed and readable *before* `log_close_file_sink()` is called
  (proving per-line flush, not buffer-until-close).

## What has been compiled and run

All of the following were built and executed in the development
environment (a Linux container with SDL2 installed via `libsdl2-dev`) as
part of this milestone — not just written:

| Area | What was verified |
|---|---|
| Host build | `cmake --build` succeeds from a clean `build/` dir, warning-free under `-Wall -Wextra -Wshadow -Wconversion -Wsign-conversion` |
| Unit tests | The current direct host test binary passes 230/230 checks across path validation, download state transitions, logging/redaction, mock and HTTP RomM APIs, storage, config/credentials, URL encoding, ZIP extraction, downloader behavior, and framebuffer layout. CI runs the same suite through `ctest`. |
| Application smoke test | `./build/rommps5` runs under `SDL_VIDEODRIVER=dummy` for a bounded number of frames (`ROMM_PS5_SMOKE_TEST_FRAMES`) and exits 0 — proves SDL init, window/renderer creation (with software-renderer fallback, which is what actually engaged in this headless environment), the mock API load, the render loop, and clean shutdown all work without crashing |
| Controller absence handling | Confirmed via the same smoke test: with no game controller attached, `ui_input_init` logs and falls back to keyboard input rather than failing |
| PS5 SDK bootstrap | `ps5-payload-dev/sdk` git tag `v0.43` built from source with `clang-18`/`lld-18` and `make DESTDIR=/opt/ps5-payload-sdk install` completed with no errors (a handful of pre-existing warnings inside the SDK's own `libc`/`libufs` code, not this project's) |
| PS5 SDK toolchain sample | The SDK's own `samples/hello_world` built against that install and produced a valid `ELF 64-bit LSB pie executable, x86-64` |
| `rommps5_core` for PS5 | Compiles **unchanged** (same source, same CMake target) against the pinned SDK: `pathval`, `download`, `log`, `config`, `storage`, `net`'s null client, `romm_api_mock`, `mockdata` — see `docs/building.md` |
| PS5 ELF build | The complete vertical slice compiles and links against the pinned SDK under `-Wall -Wextra -Wshadow -Wconversion -Wsign-conversion`, producing a PS5 ELF using UserService, Pad, VideoOut, Net, SSL, and Http2. Each published nightly artifact is verified by `file`, `readelf`, and `sha256sum` in CI. |
| GitHub Actions (both jobs) | `host-build-test` and `ps5-cross-compile` both ran for real on `ubuntu-24.04` GitHub-hosted runners and completed with `conclusion: success` — confirmed by reading the actual job logs (real `prospero-clang` invocations, real `readelf`/`sha256sum` output, real test pass, real artifact upload), not just the run's top-level status. |

**CI run for this milestone's fixes**: run
[`33905275501`](https://github.com/Crimson3076/RomM-PS5/actions/runs/33905275501)
(commit `69bc48c`), both jobs `success`, verified via job logs (135-check
test pass logged, real `readelf`/`sha256sum` output). CI-built
`rommps5-ps5` SHA-256: `9f5ea26cba4f6d4818158d774d7fd8fa59a6a1368094e17af82492368bacf8e9`
(differs from any locally-built copy — see the reproducibility caveat
below; this is the canonical artifact, downloadable from that run as
`rommps5-ps5-nightly-69bc48cd6725343314357ffb47f9fe331fc5ae8a`).

**Reproducibility caveat found this milestone**: two clean *local* builds
produced byte-identical SHA-256 output, but the CI-built ELF's SHA-256
differs from the local one. This is expected, not a bug: `-g` embeds the
absolute build directory in DWARF debug info
(`DW_AT_comp_dir`/`DW_AT_name`), and the local environment's checkout path
(`/home/user/RomM-PS5`) differs from the runner's
(`/home/runner/work/RomM-PS5/RomM-PS5`). The build is reproducible *given a
fixed checkout path*, not yet reproducible byte-for-byte *across* arbitrary
paths. A future improvement (not done this milestone) would be to add
`-fdebug-prefix-map=$PWD=.` (or similar) to normalize this. Always quote
the checksum of the specific artifact you actually have, not a
different-environment build's — they are legitimately different files.

## What remains mocked or host-only

- The desktop SDL application still uses `romm_api_mock_init()` and its
  fictional eight-game fixture. That keeps desktop UI smoke tests independent
  of a real server.
- The PS5 target does not use the mock backend. It loads real credentials,
  instantiates `http_client_ps5`, and uses `romm_api_http` plus the real
  downloader and ZIP extractor.
- Host unit tests use `mock_http_client` to exercise API parsing, fresh and
  resumed downloads, cancellation, storage refusal, and extraction without
  making external requests. These tests validate application logic, not SCE
  networking behavior.

## What has NOT been tested at all

- **Usable real `ScePad` input.** The fourth hardware test reached
  `scePadOpen`, but the first user returned by UserService was rejected as
  `USER_NOT_LOGIN`. No valid handle was obtained, so `scePadReadState` and
  actual buttons remain unverified. The next build tests every returned user
  and checks for an existing handle.
  - DualSense input via the host-only `SDL_GameController` path is
    irrelevant to the PS5 target (it's host-build-only, see
    `docs/building.md`) and remains desktop-only-tested regardless.
  - PS5 firmware version/compatibility beyond "targets 10.60" per the
    project's stated scope — this milestone's console is one data point,
    not a compatibility sweep.
- **A successful real RomM response.** The fourth test loaded the real config,
  initialized the console network libraries, and attempted `/api/platforms`,
  but `sceHttp2SendRequest` failed before a status was returned. Authentication,
  pagination, and response parsing therefore remain unverified on hardware.
- **Any on-console download or extraction.** Those paths are implemented and
  host-tested but cannot run until the initial RomM request succeeds. No game
  data has been written by this app on a PS5.
- **Large files and large libraries on hardware.** Host tests cover bounded
  synthetic data only. Nothing yet proves behavior for 100+ GB files or a
  large real library on the console.
- **Visible UI correctness.** The fourth run completed VideoOut initialization
  and invoked screen presentation without logged errors. The operator has not
  yet reported whether the pixels, colors, text, or tiled layout appeared
  correctly on the display.
- **Other consoles or firmware versions.** Current hardware evidence is from
  one CFI-1215A Z2X.

## Hardware test checklist

The full hardware test list lives in the top-level task description this
project was scoped from (valid/invalid URLs, expired/revoked tokens, USB
removal mid-transfer, etc.). Most of it still doesn't apply, because there
is no network or download code to exercise — that becomes relevant starting
Milestone 2 (RomM connection) and Milestone 3 (safe test download).

### First hardware test — DONE (see "First hardware test — RESULTS" above)

1. ~~The transfer completes and the loader accepts the ELF.~~ ✅
2. ~~It runs without crashing the loader or the console.~~ ✅
3. ~~A "RomM-PS5" toast notification appears on screen.~~ ✅
4. ~~Log lines appear in the terminal running the deploy command.~~ ✅
5. ~~`/data/romm-ps5/ps5-hello.log` exists on the console afterward.~~ ✅
   (though incomplete at the time — fixed and confirmed by the retest below)
6. `scePadOpen` reports a plausible (non-negative) handle when a DualSense
   is connected. ❌ **Not reached** — blocked by the `sceUserServiceGetInitialUser`
   failure; still not reached after the fix either, since the fix
   documents the blocker rather than resolving it (confirmed again by the
   retest below).
7. ~~The process actually exits cleanly.~~ ✅

### Retest checklist — DONE (see "Second hardware test — RESULTS" above)

Console: CFI-1215A Z2X (same unit). Artifact: CI-built `rommps5-ps5`,
SHA-256 `9f5ea26cba4f6d4818158d774d7fd8fa59a6a1368094e17af82492368bacf8e9`
(GitHub Actions run `33905275501`, commit `69bc48c`). Firmware version not
separately recorded for this retest (same console as the first test).

1. ~~The artifact still runs to completion without crashing.~~ ✅
2. ~~`sceUserServiceInitialize returned 0x... (...)` is logged.~~ ✅
   `0x0 (0)`, same success as the first test, now logged explicitly.
3. ~~`scePadInit returned 0x... (...)` is logged.~~ ✅ `0x0 (0)` — the
   first real data point on this call (never reached in the first test).
4. ~~The `"scePadOpen skipped: no SDK-verified way..."` warning appears.~~
   ✅ Confirms the code no longer calls the unverified
   `sceUserServiceGetInitialUser` function at all.
5. ~~The persistent log file now contains every line that appeared over
   TCP.~~ ✅ **Confirmed fixed** — including the `storage_discover` lines
   and the ScePad-skip warning. This was the specific regression this
   milestone targeted.
6. ~~The notification toast, hardware model log, and storage-discovery
   result are unchanged from the first test.~~ ✅ Identical values
   (same console, same storage state).
7. ~~The process still exits cleanly and the loader is ready for another
   payload afterward.~~ ✅

**Not covered by this retest, still open**: real `ScePad` controller
input (`scePadOpen`/`scePadReadState`) — see "What has NOT been tested at
all" above. Do not treat controller input as verified.
