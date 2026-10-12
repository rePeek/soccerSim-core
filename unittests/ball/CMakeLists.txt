# Ball physics contract: model validation, CCD, response and one-tick motion.
# Links only football::ball (+ foundation/geometry it declares), never simcore.

add_executable(football_ball_unit_tests
  ball_model_test.cpp
  ball_response_test.cpp
  ball_simulation_test.cpp)

target_link_libraries(football_ball_unit_tests
  PRIVATE
    football::ball
    Catch2::Catch2WithMain)

football_configure_target(football_ball_unit_tests)

add_test(NAME ball_unit COMMAND football_ball_unit_tests)
set_tests_properties(ball_unit PROPERTIES
  LABELS "unit;ball"
  TIMEOUT 10)
