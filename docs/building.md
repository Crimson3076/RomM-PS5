# Building RomM-PS5

Status: PS5 cross-compilation milestone. Two targets exist:
- **host** — the SDL2 UI + unit tests. Fully built and run in this
  environment and in CI.
- **ps5** — cross-compiles a PS5-native ELF (`src/ps5/main_ps5.c`, no SDL)
  against a pinned, source-built `ps5-payload-dev/sdk`. **Compiling and
  linking are verified** (see below and `docs/testing.md`) — it has **not**
  been run on real PS5 hardware yet. Do not describe it as "working" beyond
  "produces a real PS5 ELF that hasn't crashed anything because it hasn't
  been run" until someone confirms it on a console.

## Host target (developer machine)

This is a normal native CMake/C project. It builds and runs on Linux; it
has not been tried on macOS or Windows, though nothing in it is
intentionally Linux-only apart from a couple of POSIX calls (`statvfs`,
`access`) in the storage module, which should work unmodified on macOS and
under WSL.

### Prerequisites

- CMake 3.16+
- A C11 compiler (tested with GCC 13 and Clang, both on Ubuntu 24.04)
- SDL2 development headers/libraries

On Ubuntu/Debian:

```sh
sudo apt-get update
sudo apt-get install -y cmake libsdl2-dev
```

### Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
```

This produces:
- `build/rommps5` — the application (SDL window, mock PS5 library, DualSense
  and keyboard navigation)
- `build/tests/rommps5_tests` — the unit test suite

### Run

```sh
./build/rommps5
```

Controls (desktop keyboard mapping — see [DualSense mapping](#input-mapping)
for the controller equivalent):
- Arrow Up/Down: move focus in the game list
- Enter/Space: "select" the focused game (currently just logs the title —
  no download exists yet)
- Escape: not yet bound to anything at this screen (single-screen app so
  far); closing the window (`SDL_QUIT`) exits cleanly

### Run the unit tests

```sh
ctest --test-dir build --output-on-failure
```

or run the test binary directly for more verbose per-check output:

```sh
./build/tests/rommps5_tests
```

Tests link only against the core module library (`rommps5_core`), not SDL,
so they run in any plain container with no display — this is what
`.github/workflows/build.yml` does in CI.

### Headless smoke test

The full application (SDL window included) can also be run headlessly,
which is how CI exercises the render loop without a real display:

```sh
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ROMM_PS5_SMOKE_TEST_FRAMES=30 ./build/rommps5
```

`ROMM_PS5_SMOKE_TEST_FRAMES` makes the app render that many frames and then
exit 0 automatically, instead of waiting for a window-close event that a
headless CI runner will never send.

### Warnings

The build enables `-Wall -Wextra -Wshadow -Wconversion -Wsign-conversion`
on GCC/Clang. As of this milestone the build is warning-clean under those
flags; keep it that way — don't silence a warning without understanding it
first.

## PS5 target (cross-compilation)

The project cross-compiles against
[`ps5-payload-dev/sdk`](https://github.com/ps5-payload-dev/sdk) (see
`docs/architecture.md` §1 for why that SDK was selected over the archived
`john-tornblom/ps5-payload-sdk`), **pinned to git tag `v0.43`** — not that
project's default branch. `.github/workflows/build.yml`'s `PS5_SDK_REF` env
var is the single source of truth for this pin; bump it deliberately.

Unlike the host build, the PS5 target has **no SDL and no UI code at all**.
`ps5-payload-dev/sdk`'s own sample set (`samples/`) has no SDL example and
does not bundle SDL, and no other maintained homebrew project consulted
during this project's research was confirmed to build SDL against this
exact SDK — so rather than build around an unverified assumption, the PS5
target (`src/ps5/main_ps5.c`) instead uses the same on-screen/notification/
controller approach the SDK's own maintained samples use directly (SCE
system libraries: `SceUserService`, `SceNotification`, `ScePad`,
`libkernel_sys`). See `docs/architecture.md` for the reasoning and
`docs/testing.md` for exactly what each of those calls has and hasn't
proven.

### 1. Bootstrap the pinned SDK (from source)

Building from source (rather than downloading the SDK's prebuilt release
zip) was chosen deliberately: it avoids trusting an opaque binary and keeps
the toolchain fully reproducible from a pinned git tag plus a pinned
Debian/Ubuntu compiler package, matching this project's "no unexplained
binary blobs" policy.

```sh
sudo apt-get update
sudo apt-get install -y bash clang-18 lld-18 wget cmake

git clone --branch v0.43 --depth 1 https://github.com/ps5-payload-dev/sdk.git /tmp/ps5-sdk-src
cd /tmp/ps5-sdk-src
sudo make -j1 DESTDIR=/opt/ps5-payload-sdk install
```

`clang-18`/`lld-18` are the exact versions this project has built and
verified with (matching the SDK's own README-documented Debian
prerequisite); other clang major versions may or may not work — this has
not been tried.

### 2. Configure and build the PS5 target

```sh
export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk
cmake -S . -B build-ps5 -DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/ps5-payload-toolchain.cmake"
cmake --build build-ps5 --parallel
```

**The toolchain file must be passed via `-DCMAKE_TOOLCHAIN_FILE=...`, not
via a plain `include()` from inside `CMakeLists.txt`.** CMake only honors
`CMAKE_C_COMPILER` changes made before `project()` runs — an earlier
version of this build tried `include()`-ing the toolchain file from inside
the project body, which silently kept using the host's own compiler while
claiming to target PS5. `CMakeLists.txt` now detects the real PS5 target by
checking for `PROSPERO`, a variable the SDK's own `toolchain/prospero.cmake`
sets — that variable being set is proof the PS5 compiler is actually
active, not just that someone asked for it.

This produces `build-ps5/rommps5-ps5` — a real PS5 ELF (`ELF 64-bit LSB pie
executable, x86-64`, `NEEDED` entries for `libSceUserService.sprx`,
`libSceNotification.sprx`, `libkernel_sys.sprx`, `libScePad.sprx`,
`libSceLibcInternal.sprx`, `libSceNet.sprx`). Verify with:

```sh
file build-ps5/rommps5-ps5
readelf -h build-ps5/rommps5-ps5
sha256sum build-ps5/rommps5-ps5
```

`cmake/ps5-payload-toolchain.cmake` deliberately does not reimplement the
SDK's compiler/linker/sysroot flags itself — it just requires
`PS5_PAYLOAD_SDK` to be set and includes
`$PS5_PAYLOAD_SDK/toolchain/prospero.cmake` directly (the file the SDK's
own `samples/hello_cmake` example uses), so it can't silently drift from
whatever the SDK actually expects.

### Which modules compile unchanged for PS5

`rommps5_core` (`pathval`, `download`, `log`, `config`, `storage`, `net`'s
null client, `romm_api_mock`, `mockdata`) compiles for the PS5 target with
**zero source changes** — the same `.c` files, same `CMakeLists.txt`
target, just a different toolchain. `rommps5_ui` (SDL) and `src/main.c`
are never built for the ps5 target at all (see `CMakeLists.txt`).

### 3. Deploy the ELF to a PS5 payload loader

Send the built ELF to a jailbroken PS5 running an ELF loader that listens
on port 9021 (e.g. `ps5-payload-dev/elfldr`) — this project does not
bundle, modify, or depend on the internals of any loader, jailbreak, or
`etaHEN`/`KStuff`/`ShadowMountPlus` component.

Plain `socat` (no SDK install required — this is exactly what the SDK's own
`bin/prospero-deploy` script does internally):

```sh
socat -t 9999999 - TCP:<PS5_IP>:9021 < build-ps5/rommps5-ps5
```

e.g. for a console at `192.168.0.180`:

```sh
socat -t 9999999 - TCP:192.168.0.180:9021 < build-ps5/rommps5-ps5
```

`file`/`readelf`/`sha256sum` output for the exact artifact used in a given
test run should always be recorded alongside the test result — see the
work-session report this milestone produced for a worked example.

If the SDK is installed locally, its own deploy tool is equivalent:

```sh
export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk
"$PS5_PAYLOAD_SDK/bin/prospero-deploy" -h 192.168.0.180 -p 9021 build-ps5/rommps5-ps5
```

### What running it should do — and what's confirmed vs. still unverified

This has now been run once on real hardware (console CFI-1215A Z2X, via
`elfldr` over port 9021) — see `docs/testing.md` "First hardware test" for
the full results, exact observed output, and what changed since. Briefly:
**confirmed** on that test: the ELF runs to completion without crashing,
the "RomM-PS5" toast notification appears on screen, log lines relay live
over the `elfldr` TCP connection, `/data/romm-ps5/ps5-hello.log` is
created and retrievable, `storage_discover()` correctly finds
`/data/etaHEN/games` with real free-space numbers, and the process exits
cleanly. **Found broken** by that same test: the persistent log file was
missing several lines that appeared over TCP, and
`sceUserServiceGetInitialUser` failed. The logging defect is fixed and
hardware-confirmed. The app now uses the maintained PS5 SDL port's
`sceUserServiceGetLoginUserIdList` path. A later hardware run reached
`scePadOpen`, but Device Service rejected the returned user as
`USER_NOT_LOGIN`; no usable controller handle or input read has been
confirmed. See `docs/testing.md` for the complete results.

## Input mapping

Two separate, non-overlapping input paths exist, matching the two targets:

- **Host (SDL2)**: `SDL_GameController` (D-Pad up/down/left/right for
  navigation, Cross for confirm, Circle for cancel) — see `src/ui/input.c`.
  Exercised on desktop only; not used by the PS5 target at all.
- **PS5 (native ScePad)**: `src/ps5/pad.c` uses the function signatures and
  pad-state layout from the maintained `ps5-payload-dev/SDL` PS5 backend.
  Hardware confirms `scePadInit` and
  `sceUserServiceGetLoginUserIdList` succeed, but the first ID returned to
  this raw `elfldr` payload was rejected by `scePadOpen` with
  `0x809b0081` (`USER_NOT_LOGIN`). The next diagnostic build validates and
  tries every returned ID plus any existing handle. Until one succeeds,
  actual `scePadReadState` button input remains unverified and the app is
  not yet controllable.
