# Startup values and tactical value contracts used by the game facade.
# The board/plan/interpreter and coach command types are plain values over
# WorldState/PlayerControlSet, never a runtime player-decision policy.
add_library(football_ai_contracts INTERFACE)
add_library(football::ai_contracts ALIAS football_ai_contracts)
target_sources(football_ai_contracts INTERFACE FILE_SET HEADERS
  BASE_DIRS ${PROJECT_SOURCE_DIR}/include
  FILES
    ${PROJECT_SOURCE_DIR}/include/football/ai/ai_config.hpp
    ${PROJECT_SOURCE_DIR}/include/football/ai/tactical_board.hpp
    ${PROJECT_SOURCE_DIR}/include/football/ai/tactical_plan.hpp
    ${PROJECT_SOURCE_DIR}/include/football/ai/tactical_interpreter.hpp
    ${PROJECT_SOURCE_DIR}/include/football/ai/tactical_examples.hpp
    ${PROJECT_SOURCE_DIR}/include/football/ai/coach_command.hpp
    ${PROJECT_SOURCE_DIR}/include/football/ai/shout.hpp
    ${PROJECT_SOURCE_DIR}/include/football/ai/coach_context.hpp
    ${PROJECT_SOURCE_DIR}/include/football/ai/coach_policy.hpp)
target_compile_features(football_ai_contracts INTERFACE cxx_std_23)
target_link_libraries(football_ai_contracts INTERFACE
  football::foundation football::pitch football::sim_contracts)

# Tactical intent, coach scheduling and the AI composition root. No player
# decision layer: moves/actions require an explicit intent producer.
add_library(football_ai STATIC)
add_library(football::ai ALIAS football_ai)
target_sources(football_ai
  PRIVATE
    ai_engine.cpp ai_engine.hpp
    coach/coach_runtime.cpp coach/coach_runtime.hpp
    coach/command_executor.cpp coach/command_executor.hpp
    coach/tool_registry.cpp coach/tool_registry.hpp
    coach/coach_tool_names.cpp coach/coach_tool_names.hpp
    coach/coach_tool_schema.cpp coach/coach_tool_schema.hpp
    coach/coach_tool_decoder.cpp coach/coach_tool_decoder.hpp
    coach/coach_tool_json.cpp coach/coach_tool_json.hpp
    coach/coach_tool_executor.cpp coach/coach_tool_executor.hpp
    coach/coach_session.cpp coach/coach_session.hpp
    coach/coach_agent_loop.cpp coach/coach_agent_loop.hpp
    coach/shout_resolver.cpp coach/shout_resolver.hpp
    coach/context_collector.cpp coach/context_collector.hpp
    coach/scheduler.cpp coach/scheduler.hpp
    coach/heuristic_coach.cpp coach/heuristic_coach.hpp
    tactical_board.cpp
    tactical_plan.cpp
    tactical_interpreter.cpp)
target_link_libraries(football_ai
  PUBLIC football::sim_contracts football::ai_contracts
  # The coach agent calls models only through the provider interface, so the AI
  # module depends on the protocol layer, never on a concrete transport.
  PRIVATE football::model
  # Header-only JSON boundary for the coach tool schema and decoder. It is
  # PRIVATE so it never enters a public simcore header.
  PRIVATE nlohmann_json::nlohmann_json)
football_configure_target(football_ai)
