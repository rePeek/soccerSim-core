# Standalone Referee module unit tests (RFE-7). Value-typed rule facts let the
# rule state be exercised directly, without a running Simulation: the test drives
# AssessFoul/CheckFoul and inspects the RulingSink output. It links the sim
# domain because the Referee implementation lives there.
add_executable(football_referee_unit_tests referee_unit_test.cpp)
target_include_directories(football_referee_unit_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(football_referee_unit_tests
  PRIVATE
    football::sim
    Catch2::Catch2WithMain)
football_configure_target(football_referee_unit_tests)

add_test(NAME referee_unit COMMAND football_referee_unit_tests)
set_tests_properties(referee_unit PROPERTIES
  LABELS "unit;referee"
  TIMEOUT 20)
