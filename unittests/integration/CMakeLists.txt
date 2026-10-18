# Full autonomous-match acceptance. These run a real GameEnv match to full
# time; they are Release-only by default so `ctest --preset debug` stays fast.
# Enable explicitly in any configuration with -DFOOTBALL_INTEGRATION_TESTS=ON,
# or run the Release preset. The half duration is overridable at run time with
# FOOTBALL_INTEGRATION_HALF_SECONDS (see autonomous_match_test.cpp).
add_executable(football_sim_integration_tests autonomous_match_test.cpp)
target_link_libraries(football_sim_integration_tests
  PRIVATE
    football::simcore
    Catch2::Catch2WithMain)
football_configure_target(football_sim_integration_tests)

add_test(NAME autonomous_match COMMAND football_sim_integration_tests)
set_tests_properties(autonomous_match PROPERTIES
  LABELS "integration;match"
  TIMEOUT 3600)
