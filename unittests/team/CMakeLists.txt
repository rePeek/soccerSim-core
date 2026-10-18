# Leaf Team-domain suite: links only football::team, never Runtime/AI/actors.
add_executable(football_team_unit_tests
  team_test.cpp
  formation_preset_test.cpp)
target_link_libraries(football_team_unit_tests
  PRIVATE football::team Catch2::Catch2WithMain)
football_configure_target(football_team_unit_tests)

add_test(NAME team_unit COMMAND football_team_unit_tests)
set_tests_properties(team_unit PROPERTIES LABELS "unit;team" TIMEOUT 20)
