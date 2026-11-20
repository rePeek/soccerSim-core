# The runner is a library-internal layer (src/), so the suite links simcore and
# adds the private src include directory to reach runner.hpp.
add_executable(football_runner_unit_tests runner_test.cpp)

target_include_directories(football_runner_unit_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(football_runner_unit_tests
  PRIVATE
    football::simcore
    Catch2::Catch2WithMain)

football_configure_target(football_runner_unit_tests)

add_test(NAME runner_unit COMMAND football_runner_unit_tests)
set_tests_properties(runner_unit PROPERTIES
  LABELS "unit;runner"
  TIMEOUT 30)
