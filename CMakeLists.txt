cmake_minimum_required(VERSION 3.16)

# The PS5 toolchain (cmake/ps5-payload-toolchain.cmake, which requires
# PS5_PAYLOAD_SDK to be set) MUST be supplied via -DCMAKE_TOOLCHAIN_FILE=...
# on the configure command line, e.g.:
#   cmake -S . -B build-ps5 -DCMAKE_TOOLCHAIN_FILE=$PWD/cmake/ps5-payload-toolchain.cmake
# CMake processes CMAKE_TOOLCHAIN_FILE before project() runs, which is the
# only point at which setting CMAKE_C_COMPILER actually takes effect — an
# earlier version of this file tried to include() the toolchain from inside
# the project body instead, which silently built with the host compiler
# while claiming to target PS5. Do not repeat that mistake: this must stay
# a toolchain file passed on the command line, never an include() below.
project(rommps5 C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "Build type" FORCE)
endif()

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -Wshadow -Wconversion -Wsign-conversion)
endif()

# Target detection: cmake/ps5-payload-toolchain.cmake includes the SDK's
# own toolchain/prospero.cmake, which sets PROSPERO and PS5 to TRUE. That
# only happens when the toolchain file was supplied correctly (see the
# comment above project()), so PROSPERO being set here is proof the PS5
# compiler is actually active — not just that someone asked for it.
#   host - the developer's own machine (no toolchain file passed). Builds
#          the SDL2 UI (src/ui, src/main.c) plus the full unit test suite.
#          This is what CI's host job runs.
#   ps5  - cross-compiles against the ps5-payload-dev/sdk toolchain (pinned
#          to git tag v0.43 — see docs/building.md), producing a PS5-native
#          ELF (src/ps5/main_ps5.c) that talks to the console directly via
#          SCE system libraries instead of SDL. VERIFIED to configure,
#          compile, and link against a real, source-built SDK install
#          pinned to that tag; NOT yet run on real PS5 hardware — see
#          docs/testing.md for exactly what that means.
#
# SDL is intentionally host-only: the ps5-payload-dev/sdk's own sample set
# (samples/) has no SDL example and does not bundle SDL, and building the
# full ps5-payload-dev/SDL port was investigated and rejected — its PS5
# video backend depends on libOSMesa.so.8 at runtime, a third-party library
# with no evidence of being present on a bare jailbroken PS5 (see
# docs/architecture.md). The PS5 target instead talks to the console
# directly: sceVideoOut for graphics (src/ps5/video.c, with the GPU tiling
# math adapted from that same SDL port's source — see src/ps5/tilemap.h),
# ScePad + SceUserService for DualSense input (src/ps5/pad.c), and
# SceNet/SceSsl/SceHttp2 for networking (src/net/http_client_ps5.c).
if(PROSPERO)
  set(ROMM_PS5_IS_PS5_TARGET ON)
else()
  set(ROMM_PS5_IS_PS5_TARGET OFF)
endif()

option(ROMM_PS5_BUILD_TESTS "Build the unit test suite (host target only)" ON)

# Modules confirmed to compile unchanged for both host and ps5 targets (see
# docs/testing.md "PS5 cross-compilation" for the module-by-module result).
# None of these use SDL or any PS5-native SCE syscall — that includes
# src/ps5/tilemap.c and src/ps5/font_render.c, which despite living under
# src/ps5/ (otherwise the ps5-only-syscalls directory) are pure address/
# pixel math with no SCE calls, so they're built here to stay host-testable
# rather than only ever compiled for a target this project can't run tests
# on directly.
add_library(rommps5_core STATIC
  src/pathval/path_validate.c
  src/download/download_manager.c
  src/download/downloader.c
  src/download/zip_extract.c
  src/log/log.c
  src/config/config.c
  src/config/credentials.c
  src/storage/storage.c
  src/net/http_client_null.c
  src/net/url_encode.c
  src/romm_api/romm_api_mock.c
  src/romm_api/romm_api_http.c
  src/mockdata/mock_library.c
  src/ps5/tilemap.c
  src/ps5/font_render.c
  third_party/cjson/cJSON.c
  third_party/font8x8/font8x8_data.c
)
target_include_directories(rommps5_core PUBLIC src)
target_include_directories(rommps5_core PUBLIC third_party/cjson third_party/font8x8)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  # cJSON and font8x8's data file are vendored third-party code
  # (third_party/THIRD_PARTY_LICENSES.md) built with this project's own
  # -Wall -Wextra -Wconversion etc — neither promises to be warning-clean
  # under those, so they get their own relaxed flags rather than polluting
  # this project's warning-clean build with upstream code it doesn't
  # control. src/ps5/font_render.c, which is this project's own code, is
  # deliberately NOT included here — see its extern declaration of
  # font8x8_basic instead of including font8x8_basic.h directly.
  set(ROMM_PS5_CJSON_EXTRA_FLAGS "-Wno-conversion;-Wno-sign-conversion")
  if(ROMM_PS5_IS_PS5_TARGET)
    # The pinned SDK's own target/include/math.h defines isnan()/isinf()
    # as _Generic-style macros over qualified (volatile/const) type
    # associations, which clang always warns are unreachable — a property
    # of that system header, unrelated to anything cJSON.c (which just
    # calls isnan()/isinf() normally) does. Host builds don't hit this,
    # since the host libc's math.h doesn't use this pattern.
    list(APPEND ROMM_PS5_CJSON_EXTRA_FLAGS "-Wno-unreachable-code-generic-assoc")
  endif()
  set_source_files_properties(third_party/cjson/cJSON.c PROPERTIES
    COMPILE_OPTIONS "${ROMM_PS5_CJSON_EXTRA_FLAGS}")
  set_source_files_properties(third_party/font8x8/font8x8_data.c PROPERTIES
    COMPILE_OPTIONS "-Wno-conversion;-Wno-sign-conversion")
endif()

if(ROMM_PS5_IS_PS5_TARGET)
  # PS5-native executable: no SDL, no host UI. Talks to the console via SCE
  # system libraries directly — see src/ps5/main_ps5.c.
  add_executable(rommps5-ps5
    src/ps5/main_ps5.c
    src/ps5/pad.c
    src/ps5/video.c
    src/ps5/screens.c
    src/net/http_client_ps5.c
  )
  # SCE system libraries this ELF calls into directly — link flags copied
  # from the ps5-payload-dev/sdk v0.43 samples that use each one (browser,
  # hwinfo, http2_get: confirmed via that sample's own Makefile, which
  # links "-lSceNet -lSceSsl -lSceHttp2"). ScePad/SceVideoOut/
  # SceSystemService follow the same lib<Name>.so -> -l<Name> convention as
  # the others, confirmed against the matching sce_stubs/lib<Name>.so files
  # in that same SDK (no sample links them, since no sample uses ScePad,
  # video output, or SystemService — see src/ps5/pad.c and src/ps5/video.c
  # for what within each is sample-verified vs convention-inferred).
  target_link_libraries(rommps5-ps5 PRIVATE
    rommps5_core
    SceUserService
    kernel_sys
    ScePad
    SceNet
    SceSsl
    SceHttp2
    SceVideoOut
    SceSystemService
  )

else()
  # Host build: SDL2 UI + unit tests. Never built for the ps5 target.
  find_package(SDL2 REQUIRED)

  add_library(rommps5_ui STATIC
    src/ui/ui.c
    src/ui/input.c
  )
  target_include_directories(rommps5_ui PUBLIC src)
  if(TARGET SDL2::SDL2)
    target_link_libraries(rommps5_ui PUBLIC rommps5_core SDL2::SDL2)
  else()
    target_include_directories(rommps5_ui PUBLIC ${SDL2_INCLUDE_DIRS})
    target_link_libraries(rommps5_ui PUBLIC rommps5_core ${SDL2_LIBRARIES})
  endif()

  add_executable(rommps5 src/main.c)
  target_link_libraries(rommps5 PRIVATE rommps5_ui rommps5_core)

  if(ROMM_PS5_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
  endif()
endif()
