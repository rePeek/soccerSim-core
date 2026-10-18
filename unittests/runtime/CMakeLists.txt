# Bounded runtime harness for the simulation composition. Links the sim domain
# (no AI, no app, no football_simcore) and never runs a match to completion;
# the CTest wall-clock timeout is the guard against a single call that hangs.
# 420 s: the bounded suite is now ~4 min on the reference host (a full 11v11
# determinism case included), so 300 s was too close. The timeout only catches
# a hung Step; the bounded tick counts are the primary guard.

add_executable(football_sim_runtime_tests simulation_runtime_test.cpp)
target_link_libraries(football_sim_runtime_tests
  PRIVATE
    football::sim
    Catch2::Catch2WithMain)
# the harness includes the internal headers through the shared src/ root.
target_include_directories(football_sim_runtime_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
football_configure_target(football_sim_runtime_tests)

add_test(NAME sim_runtime COMMAND football_sim_runtime_tests)
set_tests_properties(sim_runtime PROPERTIES
  LABELS "runtime;sim"
  TIMEOUT 420)

# Pure Team configuration tests: no running Simulation or animation asset load.
add_executable(football_team_data_tests team_data_test.cpp)
target_link_libraries(football_team_data_tests PRIVATE football::sim Catch2::Catch2WithMain)
football_configure_target(football_team_data_tests)
add_test(NAME team_data_unit COMMAND football_team_data_tests)
set_tests_properties(team_data_unit PROPERTIES LABELS "unit;team" TIMEOUT 20)
