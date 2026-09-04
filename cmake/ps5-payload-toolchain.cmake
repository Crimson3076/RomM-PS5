# Thin wrapper around the ps5-payload-dev/sdk's own CMake toolchain file.
#
# We deliberately do not reimplement the PS5 cross-toolchain's compiler/
# linker/sysroot flags here — the SDK (https://github.com/ps5-payload-dev/sdk)
# ships its own toolchain/prospero.cmake, and it is what every CMake-based
# sample in the SDK (samples/hello_cmake) uses directly via the
# PS5_PAYLOAD_SDK environment variable. Duplicating those details here would
# risk silently drifting from whatever the SDK actually expects.
#
# VERIFIED: this wrapper has been exercised against a real
# ps5-payload-dev/sdk install, pinned to git tag v0.43, built from source
# with clang-18/lld-18 (see docs/building.md "PS5 target" for exact steps
# and what was actually confirmed vs. still needs real hardware).

if(NOT DEFINED ENV{PS5_PAYLOAD_SDK})
  message(FATAL_ERROR "PS5_PAYLOAD_SDK is not set; see docs/building.md")
endif()

set(PS5_PAYLOAD_SDK_TOOLCHAIN "$ENV{PS5_PAYLOAD_SDK}/toolchain/prospero.cmake")
if(NOT EXISTS "${PS5_PAYLOAD_SDK_TOOLCHAIN}")
  message(FATAL_ERROR
    "Expected to find toolchain/prospero.cmake under "
    "\$PS5_PAYLOAD_SDK=$ENV{PS5_PAYLOAD_SDK}, but it doesn't exist. Confirm "
    "PS5_PAYLOAD_SDK points at a full ps5-payload-dev/sdk install (i.e. the "
    "DESTDIR passed to that project's own 'make install', not its source "
    "checkout).")
endif()

include("${PS5_PAYLOAD_SDK_TOOLCHAIN}")
