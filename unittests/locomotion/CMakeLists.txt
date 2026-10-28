# LOCO-2.1 headless off-ball locomotion baseline. Exercises the real
# PlayerMotion::StepContinuous + PlayerBodyFacing at a fixed 100 Hz; no
# Simulation object, no Humanoid, no RNG and no animation asset. Links the
# independent football::player_command library for the production motion model.

add_executable(football_locomotion_unit_tests
  locomotion_motion_test.cpp
  locomotion_state_baseline_test.cpp)
target_link_libraries(football_locomotion_unit_tests
  PRIVATE
    football::player_command
    Catch2::Catch2WithMain)

football_configure_target(football_locomotion_unit_tests)

add_test(NAME locomotion_unit COMMAND football_locomotion_unit_tests)
set_tests_properties(locomotion_unit PROPERTIES
  LABELS "unit;locomotion;sim"
  TIMEOUT 10)