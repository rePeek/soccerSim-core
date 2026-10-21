# Bounded runtime harness for the simulation composition. Links the sim domain
# (no AI, no app, no football_simcore) and never runs a match to completion;
# the CTest wall-clock timeout is the guard against a single call that hangs.

add_executable(football_sim_runtime_tests
  simulation_runtime_test.cpp referee_ruling_test.cpp
  period_change_test.cpp match_physics_test.cpp
  team_ownership_test.cpp team_entity_test.cpp team_lifecycle_test.cpp)
target_link_libraries(football_sim_runtime_tests
  PRIVATE
    football::sim
    Catch2::Catch2WithMain)
# the harness includes the internal headers through the shared src/ root.
target_include_directories(football_sim_runtime_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
football_configure_target(football_sim_runtime_tests)

# Level 2: bounded interface/runtime integration. The full-roster physics
# invariants add a few seconds of real simulation, so the wall-clock timeout
# guards a hung Step rather than a slow host.
add_test(NAME sim_runtime COMMAND football_sim_runtime_tests)
set_tests_properties(sim_runtime PROPERTIES
  LABELS "interface;runtime;sim"
  TIMEOUT 600)

# TeamSetup adapter and runtime ActiveLineupState; no running match.
add_executable(football_team_data_tests team_data_test.cpp)
target_link_libraries(football_team_data_tests PRIVATE football::sim Catch2::Catch2WithMain)
# TeamSetup adapter and ActiveLineupState are Runtime-private implementation.
target_include_directories(football_team_data_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
football_configure_target(football_team_data_tests)
add_test(NAME team_data_unit COMMAND football_team_data_tests)
set_tests_properties(team_data_unit PROPERTIES LABELS "unit;team" TIMEOUT 20)
