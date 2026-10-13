# animation — reference motion extraction from baked clips. Header-only, links
# only football::animation.

add_executable(football_animation_unit_tests motion_clip_test.cpp)
target_link_libraries(football_animation_unit_tests
  PRIVATE
    football::animation
    Catch2::Catch2WithMain)
football_configure_target(football_animation_unit_tests)

add_test(NAME animation_unit COMMAND football_animation_unit_tests)
set_tests_properties(animation_unit PROPERTIES
  LABELS "unit;animation"
  TIMEOUT 10)
