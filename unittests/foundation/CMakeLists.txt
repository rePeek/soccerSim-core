# Foundation math/time contract: pure values, no football concepts.

add_executable(football_foundation_unit_tests
  vector3_test.cpp
  quaternion_test.cpp
  scalar_test.cpp
  tick_test.cpp
  rng_test.cpp)

target_link_libraries(football_foundation_unit_tests
  PRIVATE
    football::foundation
    Catch2::Catch2WithMain)

football_configure_target(football_foundation_unit_tests)

add_test(NAME foundation_unit COMMAND football_foundation_unit_tests)
set_tests_properties(foundation_unit PROPERTIES
  LABELS "unit;foundation"
  TIMEOUT 10)
