# LOCO-2.1 headless off-ball locomotion baseline. Exercises the real
# PlayerMotion::StepContinuous + PlayerBodyFacing at a fixed 100 Hz; no
# Simulation object, no Humanoid, no RNG and no animation asset. Links
# football::sim for the production PlayerMotion implementation.

add_executable(football_locomotion_unit_tests
  locomotion_motion_test.cpp
  locomotion_state_baseline_test.cpp)
target_link_libraries(football_locomotion_unit_tests
  PRIVATE
    football::sim
    Catch2::Catch2WithMain)
# the baseline includes the internal player/ headers from the shared src/ root.
target_include_directories(football_locomotion_unit_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)

football_configure_target(football_locomotion_unit_tests)

add_test(NAME locomotion_unit COMMAND football_locomotion_unit_tests)
set_tests_properties(locomotion_unit PROPERTIES
  LABELS "unit;locomotion;sim"
  TIMEOUT 10)