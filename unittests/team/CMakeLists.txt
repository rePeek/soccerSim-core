# Team domain suites.
#
# `football_team_unit_tests` is the leaf suite: football::team only.
# `football_team_domain_unit_tests` covers the Team/Setup/Restart-placement
# construction path with real Players and Animation, but never constructs a
# Simulation or runs a match tick.
add_executable(football_team_unit_tests
  team_test.cpp
  formation_preset_test.cpp)
target_link_libraries(football_team_unit_tests
  PRIVATE football::team Catch2::Catch2WithMain)
football_configure_target(football_team_unit_tests)

add_test(NAME team_unit COMMAND football_team_unit_tests)
set_tests_properties(team_unit PROPERTIES LABELS "unit;team" TIMEOUT 20)

add_executable(football_team_domain_unit_tests
  team_data_test.cpp
  kickoff_placement_test.cpp)
target_link_libraries(football_team_domain_unit_tests
  PRIVATE
    football::team football::restart football::player_execution
    football::animation Catch2::Catch2WithMain)
# The adapter and restart placement headers are module internals.
target_include_directories(football_team_domain_unit_tests
  PRIVATE ${PROJECT_SOURCE_DIR}/src)
football_configure_target(football_team_domain_unit_tests)
football_require_animation_catalog(football_team_domain_unit_tests)

add_test(NAME team_domain_unit COMMAND football_team_domain_unit_tests)
set_tests_properties(team_domain_unit PROPERTIES LABELS "unit;team" TIMEOUT 60)
