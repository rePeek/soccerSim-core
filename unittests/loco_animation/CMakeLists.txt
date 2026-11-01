# LOCO-2.4/2.5/2.7 — headless off-ball animation acceptance.
# Loads the canonical GLB catalog, drives the strategy over the real Motion library for
# the LOCO scenarios, and verifies that body profile changes geometry without
# changing animation selection (shared library, independent capabilities).
# The motion/pose acceptance also drives the production pose chain (warp,
# realization, stride/plant solve), so it links Player Execution; still no
# Simulation, Humanoid or RNG.

add_executable(football_loco_animation_tests
  loco_animation_acceptance_test.cpp
  loco_animation_replay_test.cpp
  loco_animation_multislot_test.cpp
  loco_animation_transition_test.cpp
  loco_animation_base_angle_test.cpp
  loco_animation_ccd_consistency_test.cpp
  loco_motion_pose_test.cpp
  rma_ik_free_acceptance_test.cpp
  rm_phase_continuity_test.cpp)
target_include_directories(football_loco_animation_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/src)
football_require_animation_cache(football_loco_animation_tests)
target_link_libraries(football_loco_animation_tests
  PRIVATE
    football::animation
    football::player_execution
    Catch2::Catch2WithMain)

football_configure_target(football_loco_animation_tests)

add_test(NAME loco_animation COMMAND football_loco_animation_tests)
set_tests_properties(loco_animation PROPERTIES
  LABELS "unit;locomotion;animation"
  TIMEOUT 60)