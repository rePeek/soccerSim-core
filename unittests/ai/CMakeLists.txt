# AI unit acceptance: tactical values/interpreter, the coach loop and the
# neutral execution boundary (no player heuristic). AIEngine is an internal
# header, so the target carries the src/ implementation root.
add_executable(football_ai_unit_tests
  tactical_schema_test.cpp
  tactical_coordinate_test.cpp
  tactical_interpreter_test.cpp
  neutral_execution_test.cpp
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
