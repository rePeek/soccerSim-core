add_executable(football_trajectory_unit_tests trajectory_test.cpp)
target_link_libraries(football_trajectory_unit_tests
  PRIVATE
    football::ball
    football::history
    nlohmann_json::nlohmann_json
    Catch2::Catch2WithMain)
football_configure_target(football_trajectory_unit_tests)

add_test(NAME trajectory_unit COMMAND football_trajectory_unit_tests)
set_tests_properties(trajectory_unit PROPERTIES
  LABELS "unit;trajectory"
  TIMEOUT 30)
