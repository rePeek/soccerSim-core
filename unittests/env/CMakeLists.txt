# Env owns the ball, the cursor and the loop, and is compiled into the shared
# library, so this suite links simcore on purpose: there is no separate Env
# module to link against. Threaded cases need a little more wall-clock room.
add_executable(football_env_unit_tests env_test.cpp)

target_link_libraries(football_env_unit_tests
  PRIVATE
    football::simcore
    Catch2::Catch2WithMain)

football_configure_target(football_env_unit_tests)

add_test(NAME env_unit COMMAND football_env_unit_tests)
set_tests_properties(env_unit PROPERTIES
  LABELS "unit;env"
  TIMEOUT 30)
