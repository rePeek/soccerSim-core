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

# The real binary stays covered too: argument handling and the printed
# observation are exactly what a caller scripts against.
if(TARGET football_app)
  add_test(NAME football_app_cli_default COMMAND football_app)
  set_tests_properties(football_app_cli_default PROPERTIES
    PASS_REGULAR_EXPRESSION "tick=0 players=22")

  add_test(NAME football_app_cli_steps COMMAND football_app --steps=10)
  set_tests_properties(football_app_cli_steps PROPERTIES
    PASS_REGULAR_EXPRESSION "tick=10 players=22")

  add_test(NAME football_app_cli_rejects_bad_count
    COMMAND football_app --steps=abc)
  set_tests_properties(football_app_cli_rejects_bad_count PROPERTIES
    WILL_FAIL TRUE)

  add_test(NAME football_app_cli_rejects_negative
    COMMAND football_app --steps=-5)
  set_tests_properties(football_app_cli_rejects_negative PROPERTIES
    WILL_FAIL TRUE)

  add_test(NAME football_app_cli_rejects_unknown_flag
    COMMAND football_app --fps=60)
  set_tests_properties(football_app_cli_rejects_unknown_flag PROPERTIES
    WILL_FAIL TRUE)
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
