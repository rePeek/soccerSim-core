# animation — the shared, immutable clip resource and its pure algorithms: clip
# selection, pose sampling/blending, entry frames, transitions, foot support and
# touch geometry. Action *decisions* (Match/MatchTouch/Trip) belong to the Player
# and are tested in unittests/player.

add_executable(football_animation_unit_tests
  motion_clip_test.cpp
  pose_sampling_test.cpp
  pose_blend_test.cpp
  selector_test.cpp
  entry_frame_continuity_test.cpp
  touch_geometry_test.cpp
  foot_support_test.cpp)
football_require_animation_cache(football_animation_unit_tests)
target_link_libraries(football_animation_unit_tests
  PRIVATE
    football::animation
    Catch2::Catch2WithMain)
football_configure_target(football_animation_unit_tests)

add_test(NAME animation_unit COMMAND football_animation_unit_tests)
set_tests_properties(animation_unit PROPERTIES
  LABELS "unit;animation"
  TIMEOUT 10)
