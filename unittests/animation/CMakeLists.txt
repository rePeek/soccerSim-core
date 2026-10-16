# animation — pure clip selection, pose sampling and blending. Header-only,
# links only football::animation. The real baked asset is used only to check
# that the pure algorithms match the shipped library.

add_executable(football_animation_unit_tests
  motion_clip_test.cpp
  pose_sampling_test.cpp
  pose_blend_test.cpp
  selector_test.cpp
  select_clip_test.cpp
  select_movement_test.cpp
  movement_selection_test.cpp
  match_test.cpp
  entry_frame_test.cpp
  touch_match_test.cpp
  touch_scoring_test.cpp
  touch_geometry_test.cpp)
target_compile_definitions(football_animation_unit_tests PRIVATE
  GFOOTBALL_BAKED_ANIM_PATH="${PROJECT_SOURCE_DIR}/assets/runtime/animations.simanim")
target_link_libraries(football_animation_unit_tests
  PRIVATE
    football::animation
    Catch2::Catch2WithMain)
football_configure_target(football_animation_unit_tests)

add_test(NAME animation_unit COMMAND football_animation_unit_tests)
set_tests_properties(animation_unit PROPERTIES
  LABELS "unit;animation"
  TIMEOUT 10)
