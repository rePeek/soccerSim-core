# animation — pure clip selection, pose sampling and blending. Header-only,
# links only football::animation.

add_executable(football_animation_unit_tests
  motion_clip_test.cpp
  pose_sampling_test.cpp
  pose_blend_test.cpp
  selector_test.cpp)
target_link_libraries(football_animation_unit_tests
  PRIVATE
    football::animation
    Catch2::Catch2WithMain)
football_configure_target(football_animation_unit_tests)

add_test(NAME animation_unit COMMAND football_animation_unit_tests)
set_tests_properties(animation_unit PROPERTIES
  LABELS "unit;animation"
  TIMEOUT 10)
