# Bounded public-API runtime regression. It links only the Runtime archive and
# reaches the match through Init/Step/Observe/ReadEvents/Result; there is no
# internal test-access shim. Set FOOTBALL_TRACE_PATH when running the trace
# case to export a per-tick file for a manual before/after diff.
add_executable(football_sim_runtime_tests runtime_trace_test.cpp)
target_include_directories(football_sim_runtime_tests
  PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(football_sim_runtime_tests
  PRIVATE football::sim Catch2::Catch2WithMain)
football_configure_target(football_sim_runtime_tests)
football_require_animation_catalog(football_sim_runtime_tests)

add_test(NAME sim_runtime COMMAND football_sim_runtime_tests)
set_tests_properties(sim_runtime PROPERTIES
  LABELS "interface;runtime;sim"
  TIMEOUT 600)
