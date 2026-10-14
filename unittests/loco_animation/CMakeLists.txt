# LOCO-2.4/2.5/2.7 — headless off-ball animation acceptance.
# Loads the baked asset, drives the strategy over the real Motion library for
# the LOCO scenarios, and verifies that body profile changes geometry without
# changing animation selection (shared library, independent capabilities).
# Links only animation + foundation; no Simulation/Humanoid/RNG.

add_executable(football_loco_animation_tests
  loco_animation_acceptance_test.cpp
  loco_animation_replay_test.cpp
  loco_animation_base_angle_test.cpp
  loco_animation_ccd_consistency_test.cpp)
target_include_directories(football_loco_animation_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/src)
target_compile_definitions(football_loco_animation_tests PRIVATE
  GFOOTBALL_BAKED_ANIM_PATH="${PROJECT_SOURCE_DIR}/assets/runtime/animations.simanim")
target_link_libraries(football_loco_animation_tests
  PRIVATE
    football::animation
    Catch2::Catch2WithMain)

football_configure_target(football_loco_animation_tests)

add_test(NAME loco_animation COMMAND football_loco_animation_tests)
set_tests_properties(loco_animation PROPERTIES
  LABELS "unit;locomotion;animation"
  TIMEOUT 10)