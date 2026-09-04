# Building RomM-PS5

Status: Milestone 1 (application foundation). This document describes the
**host** build, which is the only target actually compiled and run so far.
The **ps5** cross-compilation target is *configured* but **UNVERIFIED** —
read the warning in [PS5 target](#ps5-target-cross-compilation-unverified)
before relying on it.

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

## PS5 target (cross-compilation) — UNVERIFIED

The project is set up to cross-compile against
[`ps5-payload-dev/sdk`](https://github.com/ps5-payload-dev/sdk) (see
`docs/architecture.md` §1 for why that SDK was selected over the archived
`john-tornblom/ps5-payload-sdk`).

**This path has not been exercised in this project.** The SDK's toolchain
was not available in the environment this Milestone 1 foundation was built
in, so:
- It is unknown whether `find_package(SDL2)` resolves correctly against the
  SDK's sysroot the way it does against the host's system SDL2.
- It is unknown whether the mock-only modules in this milestone
  (`rommps5_core`) compile cleanly against the SDK's libc without
  modification.
- The resulting binary has never been run on a PS5.

Do not describe the PS5 build as "working" until someone has actually run
these steps against a real SDK install and a real console. When that
happens, replace this section with real, tested instructions and update
`docs/compatibility.md`.

### Intended steps (once the SDK is available)

1. Obtain and build `ps5-payload-dev/sdk` per its own instructions, and set:
   ```sh
   export PS5_PAYLOAD_SDK=/path/to/ps5-payload-dev-sdk
   ```
2. Configure with the ps5 target:
   ```sh
   cmake -S . -B build-ps5 -DROMM_PS5_TARGET_PLATFORM=ps5
   cmake --build build-ps5 --parallel
   ```
   `cmake/ps5-payload-toolchain.cmake` deliberately does not reimplement the
   SDK's compiler/linker/sysroot flags — it just requires `PS5_PAYLOAD_SDK`
   to be set and includes `$PS5_PAYLOAD_SDK/toolchain.cmake` directly, so it
   can't silently drift from whatever the SDK actually expects.
3. Deploy the resulting ELF to a jailbroken PS5 running an ELF loader
   (e.g. `ps5-payload-dev/elfldr`) per that project's own instructions —
   this project does not bundle or modify any loader.

## Input mapping

Real DualSense mapping is implemented via `SDL_GameController` (D-Pad up/
down/left/right for navigation, Cross for confirm, Circle for cancel) — see
`src/ui/input.c`. This has been exercised on desktop only so far (no
physical DualSense was available in this environment); it should work
identically on PS5 once `ps5-payload-dev/SDL` exposes the pad as a standard
game controller, but that is itself unverified — see `docs/testing.md`.
