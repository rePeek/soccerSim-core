add_executable(football_history_unit_tests history_test.cpp)
target_link_libraries(football_history_unit_tests
  PRIVATE
    football::history
    nlohmann_json::nlohmann_json
    Catch2::Catch2WithMain)
football_configure_target(football_history_unit_tests)

add_test(NAME history_unit COMMAND football_history_unit_tests)
set_tests_properties(history_unit PROPERTIES
  LABELS "unit;history"
  TIMEOUT 10)
