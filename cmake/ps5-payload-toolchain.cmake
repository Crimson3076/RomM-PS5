# Thin wrapper around the ps5-payload-dev/sdk's own toolchain file.
#
# We deliberately do not reimplement the PS5 cross-toolchain's compiler/
# linker/sysroot flags here — the SDK (https://github.com/ps5-payload-dev/sdk)
# ships its own toolchain.cmake, and every reference project in
# docs/architecture.md consumes that file directly via the PS5_PAYLOAD_SDK
# environment variable rather than hand-rolling the flags. Duplicating those
# details here, without a copy of the SDK available to verify them against,
# would risk silently drifting from whatever the SDK actually expects.
#
# UNVERIFIED: this wrapper has not been exercised against a real
# ps5-payload-dev/sdk install. See docs/building.md before relying on it.

if(NOT DEFINED ENV{PS5_PAYLOAD_SDK})
  message(FATAL_ERROR "PS5_PAYLOAD_SDK is not set; see docs/building.md")
endif()

set(PS5_PAYLOAD_SDK_TOOLCHAIN "$ENV{PS5_PAYLOAD_SDK}/toolchain.cmake")
if(NOT EXISTS "${PS5_PAYLOAD_SDK_TOOLCHAIN}")
  message(FATAL_ERROR
    "Expected to find a toolchain.cmake at ${PS5_PAYLOAD_SDK_TOOLCHAIN} "
    "(from \$PS5_PAYLOAD_SDK=$ENV{PS5_PAYLOAD_SDK}), but it doesn't exist. "
    "Confirm PS5_PAYLOAD_SDK points at a full ps5-payload-dev/sdk checkout.")
endif()

include("${PS5_PAYLOAD_SDK_TOOLCHAIN}")
