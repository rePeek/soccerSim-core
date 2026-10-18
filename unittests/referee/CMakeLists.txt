# Standalone Referee module unit tests (REF-8): links only football::referee and
# the public value contracts. Because the rules consume value facts, the foul,
# advantage and offside surfaces are exercised without a Simulation, Player or
# RuleCommandSink.
add_executable(football_referee_unit_tests referee_unit_test.cpp)
target_include_directories(football_referee_unit_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(football_referee_unit_tests
  PRIVATE
    football::referee
    Catch2::Catch2WithMain)
football_configure_target(football_referee_unit_tests)

add_test(NAME referee_unit COMMAND football_referee_unit_tests)
set_tests_properties(referee_unit PROPERTIES
  LABELS "unit;referee"
  TIMEOUT 20)
