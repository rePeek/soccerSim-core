# Catch2 suites for the executable side of the project: the CLI argument
# parsing, the app fixtures/importer, and the exact composition the CLI runs.
# These link the app fixtures and the core shared library but never become
# part of the core itself.

add_executable(football_app_test
  app_args_test.cpp
  app_fixtures_test.cpp
  app_cli_test.cpp)
target_link_libraries(football_app_test PRIVATE
  football_app_support ${OUTPUT_LIB_NAME})
# Keep the Catch2 target out of the archive-order chain: it is an interface
# target for the test executable only.
target_link_libraries(football_app_test PRIVATE Catch2::Catch2WithMain)
football_configure_target(football_app_test)

catch_discover_tests(football_app_test
  PROPERTIES LABELS app)

# The real CLI always runs to the referee's final whistle; no external tick budget.
if(TARGET football_app)
  add_test(NAME football_app_cli_default COMMAND football_app)
  set_tests_properties(football_app_cli_default PROPERTIES
    PASS_REGULAR_EXPRESSION "home_score=[0-9]+ away_score=[0-9]+ outcome=.* duration_ticks=[1-9][0-9]*"
    TIMEOUT 180)
  add_test(NAME football_app_cli_short_match COMMAND football_app --half-duration-ms=1800)
  set_tests_properties(football_app_cli_short_match PROPERTIES
    PASS_REGULAR_EXPRESSION "home_score=0 away_score=0 outcome=draw duration_ticks=[1-9][0-9]*")
  foreach(value IN ITEMS abc -5 0 18446744073709551615)
    add_test(NAME football_app_cli_rejects_duration_${value}
      COMMAND football_app --half-duration-ms=${value})
    set_tests_properties(football_app_cli_rejects_duration_${value} PROPERTIES WILL_FAIL TRUE)
  endforeach()
  add_test(NAME football_app_cli_rejects_tick_budget COMMAND football_app --steps=10)
  set_tests_properties(football_app_cli_rejects_tick_budget PROPERTIES WILL_FAIL TRUE)
endif()


# Runtime computation ownership has its own focused tests. App descriptions
# remain test inputs and never become a runtime dependency.
add_executable(football_sim_computation_test sim_computation_test.cpp)
target_link_libraries(football_sim_computation_test PRIVATE
  ${OUTPUT_LIB_NAME} football_app_support Catch2::Catch2WithMain)
football_configure_target(football_sim_computation_test)
catch_discover_tests(football_sim_computation_test PROPERTIES LABELS sim)

add_test(NAME module_dependency_guard_negative_test
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/module_dependency_guard_test.sh
    ${PROJECT_SOURCE_DIR}/tools/module_dependency_guard.sh
    ${PROJECT_BINARY_DIR}/module_dependencies.txt)

add_test(NAME module_source_ownership_guard_negative_test
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/module_source_ownership_guard_test.sh
    ${PROJECT_SOURCE_DIR}/tools/module_source_ownership_guard.sh
    ${PROJECT_SOURCE_DIR}/src ${PROJECT_BINARY_DIR}/module_dependencies.txt)

# This target proves that policy compiles/links without sim or libgame.
add_executable(football_default_ai_test default_ai_test.cpp)
target_link_libraries(football_default_ai_test PRIVATE
  football_ai Catch2::Catch2WithMain)
football_configure_target(football_default_ai_test)
catch_discover_tests(football_default_ai_test PROPERTIES LABELS ai)

add_executable(football_sim_control_boundary_test
  sim_control_boundary_test.cpp sim_match_lifecycle_test.cpp restart_placement_test.cpp)
target_link_libraries(football_sim_control_boundary_test PRIVATE
  ${OUTPUT_LIB_NAME} football_app_support football_app_input Catch2::Catch2WithMain)
football_configure_target(football_sim_control_boundary_test)
catch_discover_tests(football_sim_control_boundary_test PROPERTIES LABELS sim)

# Input and selection operate entirely on values, independently of sim/libgame.
add_executable(football_app_input_test app_input_test.cpp)
target_link_libraries(football_app_input_test PRIVATE football_app_input Catch2::Catch2WithMain)
football_configure_target(football_app_input_test)
catch_discover_tests(football_app_input_test PROPERTIES LABELS app)
