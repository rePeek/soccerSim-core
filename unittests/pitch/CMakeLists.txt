# football::pitch — domain ownership of the static pitch scene. Links only the
# pitch module (and its geometry/foundation dependencies); no Ball, no
# Simulation.

add_executable(football_pitch_unit_tests pitch_test.cpp)
target_link_libraries(football_pitch_unit_tests
  PRIVATE
    football::pitch
    Catch2::Catch2WithMain)
football_configure_target(football_pitch_unit_tests)

add_test(NAME pitch_unit COMMAND football_pitch_unit_tests)
set_tests_properties(pitch_unit PROPERTIES
  LABELS "unit;pitch"
  TIMEOUT 10)
