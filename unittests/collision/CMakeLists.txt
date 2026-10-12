# football::collision — generic swept-sphere CCD. Links only the collision
# module (geometry + foundation); never ball/pitch/player.

add_executable(football_collision_unit_tests sweep_test.cpp)
target_link_libraries(football_collision_unit_tests
  PRIVATE
    football::collision
    Catch2::Catch2WithMain)
football_configure_target(football_collision_unit_tests)

add_test(NAME collision_unit COMMAND football_collision_unit_tests)
set_tests_properties(collision_unit PROPERTIES
  LABELS "unit;collision"
  TIMEOUT 10)
