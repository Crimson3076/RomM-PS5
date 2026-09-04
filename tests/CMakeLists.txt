# Host-only unit tests. No SDL/UI dependency — these link only against
# rommps5_core so they stay runnable in a plain CI container with no
# display, matching the "desktop-testable" requirement.
add_executable(rommps5_tests
  test_main.c
  test_path_validate.c
  test_download_state.c
  test_log_redact.c
  test_romm_api_mock.c
  test_storage.c
  test_config.c
)
target_link_libraries(rommps5_tests PRIVATE rommps5_core)
target_include_directories(rommps5_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_test(NAME rommps5_tests COMMAND rommps5_tests)
