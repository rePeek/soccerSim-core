# animation — the shared, immutable clip resource and its pure algorithms: pose
# sampling/blending, entry frames, transitions, foot support and touch geometry.
# Candidate/action selection belongs to the Player (unittests/player,
# unittests/player_decision).

add_executable(football_animation_unit_tests
  catalog_loader_test.cpp
  motion_clip_test.cpp
  pose_sampling_test.cpp
  pose_blend_test.cpp
  entry_frame_continuity_test.cpp
  touch_geometry_test.cpp
  foot_support_test.cpp
  motion_quality_test.cpp
  reference_sample_test.cpp)
football_require_animation_cache(football_animation_unit_tests)
# Import verification may inspect support workspaces; production consumers cannot.
target_include_directories(football_animation_unit_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/src/animation/import)
target_link_libraries(football_animation_unit_tests
  PRIVATE
    football::animation
    nlohmann_json::nlohmann_json
    Catch2::Catch2WithMain)
football_configure_target(football_animation_unit_tests)

add_test(NAME animation_unit COMMAND football_animation_unit_tests)
set_tests_properties(animation_unit PROPERTIES
  LABELS "unit;animation"
  TIMEOUT 60)
