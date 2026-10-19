# AI protocol acceptance: the frozen IPlayerPolicy contract and the Default
# equivalence between AIEngine and the public DefaultAI baseline. AIEngine is an
# internal header, so the target carries the src/ implementation root.
add_executable(football_ai_unit_tests
  default_policy_equivalence_test.cpp
  eliza_off_ball_test.cpp
  eliza_on_ball_test.cpp
  eliza_keeper_tactics_test.cpp
  eliza_frequency_test.cpp
  policy_switch_test.cpp
  tactical_schema_test.cpp
  tactical_coordinate_test.cpp
  tactical_interpreter_test.cpp
  tactical_response_test.cpp
  heuristic_policy_test.cpp
  coach_command_test.cpp
  coach_loop_test.cpp
  shout_lifecycle_test.cpp
  coach_context_test.cpp
  coach_scheduler_test.cpp
  coach_tool_decoder_test.cpp
  coach_tool_schema_test.cpp
  coach_agent_loop_test.cpp
  tactical_examples_test.cpp)
target_include_directories(football_ai_unit_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(football_ai_unit_tests
  PRIVATE
    football::ai
    football::sim_contracts
    # The coach loop header exposes the model provider seam.
    football::model
    football::model_mock
    # nlohmann/json is a PRIVATE football_ai dependency, so a test that
    # includes an internal tool header links it directly.
    nlohmann_json::nlohmann_json
    Catch2::Catch2WithMain)
football_configure_target(football_ai_unit_tests)

add_test(NAME ai_unit COMMAND football_ai_unit_tests)
set_tests_properties(ai_unit PROPERTIES
  LABELS "unit;ai"
  TIMEOUT 30)
