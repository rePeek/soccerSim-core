# Event module unit tests: the append-only MatchEvent log and its incremental
# cursor. Header-only, so it links only the public value contracts; the internal
# event_log.hpp is reached through the shared src/ root.
add_executable(football_event_unit_tests event_log_test.cpp)
target_include_directories(football_event_unit_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(football_event_unit_tests
  PRIVATE
    football::sim_contracts
    Catch2::Catch2WithMain)
football_configure_target(football_event_unit_tests)

add_test(NAME event_unit COMMAND football_event_unit_tests)
set_tests_properties(event_unit PROPERTIES
  LABELS "unit;event"
  TIMEOUT 20)
