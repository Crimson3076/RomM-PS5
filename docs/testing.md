# Testing status

This document tracks what has actually been built, run, and verified —
and, just as importantly, what has not — following the project rule that
AI-generated code is untrusted until reviewed, compiled, and tested, and
that untested functionality must never be described as working.

Status as of the SceUserService/logging fix milestone (follows the PS5
cross-compilation milestone, which followed Milestone 1).

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

## Fixes applied in response — NOT YET HARDWARE-VERIFIED

Everything in this section has been compiled, cross-compiled for PS5, and
covered by new host-side tests. **None of it has been run on a console
yet.** Do not treat it as hardware-confirmed until a retest happens — see
this milestone's hardware retest handoff for the exact artifact to use.

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
- **Consequence for ScePad**: `scePadOpen`/`scePadClose` are only called
  when a verified user id is available — which, per the above, never
  happens right now, so they are skipped and logged as a documented
  blocker (`"scePadOpen skipped: no SDK-verified way to obtain a real
  user id..."`) rather than attempted with a guessed value.
  `scePadInit()` (which takes no arguments, so there is nothing about its
  call shape to guess) is still called unconditionally; its return value
  is now logged in both hex and signed decimal.
- **`sceUserServiceInitialize`/`sceNotificationSend`/`scePadInit` return
  values are now all logged in both hex and signed decimal** (previously
  only pass/fail was logged for some of them), e.g.
  `sceUserServiceInitialize returned 0x0 (0)`.
- **`sceUserServiceTerminate` is now only called if `sceUserServiceInitialize`
  actually returned 0** — previously it was called unconditionally.
- **Logging consolidated behind one API.** `src/log/log.c` now owns an
  optional persistent-file sink (`log_init_file_sink()`/
  `log_close_file_sink()`); every `log_message()` call (i.e. every
  `log_info`/`log_warn`/`log_error`/`log_debug` in the whole codebase)
  formats its line once and writes the identical bytes to both stderr and
  the file sink (if open), flushing the file immediately on every line —
  not buffered until shutdown. `src/ps5/main_ps5.c`'s previous hand-rolled
  `logfile_line()` duplicate-logging helper was deleted entirely; nothing
  in this project's own code calls `printf`/`fprintf` for application
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
| Unit tests | `ctest` and the direct test binary both pass: 135/135 checks across path validation, download state transitions, auth-header redaction, mock RomM API search/sort/pagination, storage discovery, config load/save, and (new this milestone) persistent-log file-sink fan-out |
| Application smoke test | `./build/rommps5` runs under `SDL_VIDEODRIVER=dummy` for a bounded number of frames (`ROMM_PS5_SMOKE_TEST_FRAMES`) and exits 0 — proves SDL init, window/renderer creation (with software-renderer fallback, which is what actually engaged in this headless environment), the mock API load, the render loop, and clean shutdown all work without crashing |
| Controller absence handling | Confirmed via the same smoke test: with no game controller attached, `ui_input_init` logs and falls back to keyboard input rather than failing |
| PS5 SDK bootstrap | `ps5-payload-dev/sdk` git tag `v0.43` built from source with `clang-18`/`lld-18` and `make DESTDIR=/opt/ps5-payload-sdk install` completed with no errors (a handful of pre-existing warnings inside the SDK's own `libc`/`libufs` code, not this project's) |
| PS5 SDK toolchain sample | The SDK's own `samples/hello_world` built against that install and produced a valid `ELF 64-bit LSB pie executable, x86-64` |
| `rommps5_core` for PS5 | Compiles **unchanged** (same source, same CMake target) against the pinned SDK: `pathval`, `download`, `log`, `config`, `storage`, `net`'s null client, `romm_api_mock`, `mockdata` — see `docs/building.md` |
| PS5 ELF build | `src/ps5/main_ps5.c` compiles and links against the pinned SDK, `-Wall -Wextra -Wshadow -Wconversion -Wsign-conversion` warning-clean, producing a real PS5 ELF with correct `NEEDED .sprx` entries for `libSceUserService`, `libSceNotification`, `libkernel_sys`, `libScePad`, `libSceLibcInternal`, `libSceNet` |
| GitHub Actions (both jobs) | `host-build-test` and `ps5-cross-compile` both ran for real on `ubuntu-24.04` GitHub-hosted runners and completed with `conclusion: success` — confirmed by reading the actual job logs (real `prospero-clang` invocations, real `readelf`/`sha256sum` output, real 115-check test pass, real artifact upload), not just the run's top-level status. See this milestone's session report for the run URL and IDs. |

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

- **The fixes in this milestone, on real hardware.** The SceUserService
  hex/decimal logging, the "skip ScePad, don't guess" blocker handling,
  and the entire persistent-logging fan-out rewrite have only been
  compiled (host + PS5 cross-compile) and covered by new host-side tests
  — see "Fixes applied in response" above. **Not one byte of this
  milestone's code has run on a console.** Do not describe any of it as
  hardware-verified until a retest confirms it — see this milestone's
  hardware retest handoff for the exact artifact and steps.
- **Real `ScePad` behavior, still entirely unknown.** The first hardware
  test never reached `scePadOpen` at all (see "First hardware test"
  above), and this milestone's fix makes that skip deliberate rather than
  accidental — it does not make ScePad work. Whether `scePadInit()`
  itself behaves sanely on this console, and whether any path to a real
  user id (and therefore `scePadOpen`) exists at all, remain completely
  open questions. `scePadReadState` (actual button/stick state) is still
  never called at all — deliberately deferred; see `docs/building.md`
  "Input mapping".
  - DualSense input via the host-only `SDL_GameController` path is
    irrelevant to the PS5 target (it's host-build-only, see
    `docs/building.md`) and remains desktop-only-tested regardless.
  - PS5 firmware version/compatibility beyond "targets 10.60" per the
    project's stated scope — this milestone's console is one data point,
    not a compatibility sweep.
- **Whether the fixed persistent log is actually complete on hardware.**
  The host-side tests (`test_log_file_sink.c`) prove the fan-out mechanism
  works correctly in general, using synthetic messages shaped like the
  real ones — they cannot prove the *actual* `/data/romm-ps5/ps5-hello.log`
  produced by a real run on this console now matches the real TCP output
  line-for-line. That specific comparison is exactly what the hardware
  retest handoff below asks for.
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
- **Any filesystem write of a game file.** `storage_discover()`'s
  read-only discovery has now been confirmed against a real fixed
  candidate path on real hardware (`/data/etaHEN/games`, see "First
  hardware test" above) as well as temp directories in host tests (see
  `tests/test_storage.c`). No code anywhere in this project writes a game
  file (or any large file) to a destination — that's still entirely
  unbuilt.
- **GitHub Actions CI itself.** `.github/workflows/build.yml` now has two
  jobs, `host-build-test` and `ps5-cross-compile`; both were written to
  mirror exactly the commands verified locally (see above). See this
  milestone's session report for the actual observed run status — do not
  trust either job as a gate until you've confirmed its current status
  yourself, since a workflow file can drift from what actually ran.

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
   (though incomplete — see "Fixes applied", now addressed but unverified)
6. `scePadOpen` reports a plausible (non-negative) handle when a DualSense
   is connected. ❌ **Not reached** — blocked by the `sceUserServiceGetInitialUser`
   failure; still not reached after this milestone's fix either, since
   that fix documents the blocker rather than resolving it.
7. ~~The process actually exits cleanly.~~ ✅

### Retest checklist — for the artifact produced by THIS milestone

Not yet run. See the hardware retest handoff (this milestone's session
report) for the exact artifact, deploy command, and expected output. Confirm:
1. The artifact still runs to completion without crashing.
2. `sceUserServiceInitialize returned 0x... (...)` is logged (hex + decimal
   both present) — expect the same success as before (`0x0 (0)`), now just
   logged explicitly instead of only on failure.
3. `scePadInit returned 0x... (...)` is logged.
4. The `"scePadOpen skipped: no SDK-verified way..."` warning appears —
   confirms the code no longer calls the unverified
   `sceUserServiceGetInitialUser` function at all.
5. **The persistent log file now contains every line that appeared over
   TCP**, including the `storage_discover` lines and the ScePad-skip
   warning — this is the specific regression this milestone fixed; retest
   must confirm the fix, not just that the app still runs.
6. The notification toast, hardware model log, and storage-discovery
   result are unchanged from the first test (same console, same expected
   values).
7. The process still exits cleanly and the loader is ready for another
   payload afterward.

Record the result of each step (and the console's firmware version) here
once it's been tried, rather than only in a session report, so this file
stays the single source of truth for what's actually been confirmed on
hardware.
