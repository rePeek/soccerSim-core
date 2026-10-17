# AI protocol acceptance: the frozen IPlayerPolicy contract and the Default
# equivalence between AIEngine and the public DefaultAI baseline. AIEngine is an
# internal header, so the target carries the src/ implementation root.
add_executable(football_ai_unit_tests
  default_policy_equivalence_test.cpp
  eliza_off_ball_test.cpp
  eliza_on_ball_test.cpp
  eliza_keeper_tactics_test.cpp
  eliza_frequency_test.cpp
  policy_switch_test.cpp)
target_include_directories(football_ai_unit_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(football_ai_unit_tests
  PRIVATE
    football::ai
    football::sim_contracts
    Catch2::Catch2WithMain)
football_configure_target(football_ai_unit_tests)

add_test(NAME ai_unit COMMAND football_ai_unit_tests)
set_tests_properties(ai_unit PROPERTIES
  LABELS "unit;ai"
  TIMEOUT 30)
