# Referee rule values are independent of Runtime: pure value facts in, verdict
# out. This suite links only football::referee (foundation/pitch closure).
add_executable(football_referee_unit_tests denied_goal_opportunity_test.cpp)
target_link_libraries(football_referee_unit_tests
  PRIVATE football::referee Catch2::Catch2WithMain)
target_include_directories(football_referee_unit_tests
  PRIVATE ${PROJECT_SOURCE_DIR}/src)
football_configure_target(football_referee_unit_tests)
add_test(NAME referee_unit COMMAND football_referee_unit_tests)
set_tests_properties(referee_unit PROPERTIES LABELS "unit;referee" TIMEOUT 20)
