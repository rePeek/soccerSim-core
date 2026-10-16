# Bounded runtime harness for the simulation composition. Links the sim domain
# (no AI, no app, no football_simcore) and never runs a match to completion;
# the CTest wall-clock timeout is the guard against a single call that hangs.
# 300 s: the bounded suite has grown to ~3 min on the reference host, so the
# old 180 s wrapped into a false timeout. It still catches a hung Step.

add_executable(football_sim_runtime_tests simulation_runtime_test.cpp)
target_link_libraries(football_sim_runtime_tests
  PRIVATE
    football::sim
    Catch2::Catch2WithMain)
football_configure_target(football_sim_runtime_tests)

add_test(NAME sim_runtime COMMAND football_sim_runtime_tests)
set_tests_properties(sim_runtime PROPERTIES
  LABELS "runtime;sim"
  TIMEOUT 300)
