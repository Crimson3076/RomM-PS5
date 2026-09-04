cmake_minimum_required(VERSION 3.16)
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

# ROMM_PS5_TARGET_PLATFORM selects what we're building for:
#   host - the developer's own machine (Linux, this is the only target
#          actually built and run in CI and in this repository's own
#          development so far — see docs/building.md).
#   ps5  - cross-compiles against the ps5-payload-dev/sdk toolchain. This
#          path is CONFIGURED but UNVERIFIED: the SDK's proprietary-adjacent
#          clang toolchain is not available in every development
#          environment (including the one this Milestone 1 foundation was
#          built in), so the ps5 target has not actually been compiled or
#          run on hardware yet. Do not treat it as working until someone
#          with the SDK installed confirms it builds and runs on a real
#          console. See docs/architecture.md and docs/building.md.
set(ROMM_PS5_TARGET_PLATFORM "host" CACHE STRING "Build target: host or ps5")
set_property(CACHE ROMM_PS5_TARGET_PLATFORM PROPERTY STRINGS host ps5)

if(ROMM_PS5_TARGET_PLATFORM STREQUAL "ps5")
  if(NOT DEFINED ENV{PS5_PAYLOAD_SDK})
    message(FATAL_ERROR
      "ROMM_PS5_TARGET_PLATFORM=ps5 requires the PS5_PAYLOAD_SDK environment "
      "variable to point at a ps5-payload-dev/sdk checkout (see "
      "docs/building.md). This toolchain integration is UNVERIFIED — it has "
      "not been exercised against a real SDK install in this project yet.")
  endif()
  include(cmake/ps5-payload-toolchain.cmake)
endif()

option(ROMM_PS5_BUILD_TESTS "Build the unit test suite (host target only)" ON)

add_library(rommps5_core STATIC
  src/pathval/path_validate.c
  src/download/download_manager.c
  src/log/log.c
  src/config/config.c
  src/storage/storage.c
  src/net/http_client_null.c
  src/romm_api/romm_api_mock.c
  src/mockdata/mock_library.c
)
target_include_directories(rommps5_core PUBLIC src)

if(NOT ROMM_PS5_TARGET_PLATFORM STREQUAL "ps5")
  # Host build: link against the system SDL2 development package.
  find_package(SDL2 REQUIRED)
else()
  # PS5 cross build: expect the SDK's own SDL2 port to be discoverable the
  # same way (via its sysroot's pkg-config/CMake config). UNVERIFIED — see
  # the FATAL_ERROR message above.
  find_package(SDL2 REQUIRED)
endif()

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

if(ROMM_PS5_BUILD_TESTS AND NOT ROMM_PS5_TARGET_PLATFORM STREQUAL "ps5")
  enable_testing()
  add_subdirectory(tests)
endif()
