# Team domain suites.
#
# `football_team_unit_tests` is the leaf suite: football::team only, no
# Runtime/AI/actors. `football_team_composition_unit_tests` covers the Team
# lifecycle that needs the real Runtime-owned construction path (TeamSetup
# adapter, formation build, active lineup and Player ownership). It links the
# sim domain but never runs a match: it places players and inspects ownership.
add_executable(football_team_unit_tests
  team_test.cpp
  formation_preset_test.cpp)
target_link_libraries(football_team_unit_tests
  PRIVATE football::team Catch2::Catch2WithMain)
football_configure_target(football_team_unit_tests)

add_test(NAME team_unit COMMAND football_team_unit_tests)
set_tests_properties(team_unit PROPERTIES LABELS "unit;team" TIMEOUT 20)

add_executable(football_team_composition_unit_tests
  team_entity_test.cpp
  team_ownership_test.cpp
  team_lifecycle_test.cpp
  team_data_test.cpp)
target_link_libraries(football_team_composition_unit_tests
  PRIVATE football::sim Catch2::Catch2WithMain)
# The TeamSetup adapter and ActiveLineupState are Runtime-private, like every
# other internal header these cases inspect.
target_include_directories(football_team_composition_unit_tests
  PRIVATE ${PROJECT_SOURCE_DIR}/src)
football_configure_target(football_team_composition_unit_tests)

add_test(NAME team_composition_unit COMMAND football_team_composition_unit_tests)
set_tests_properties(team_composition_unit PROPERTIES
  LABELS "unit;team"
  TIMEOUT 60)
