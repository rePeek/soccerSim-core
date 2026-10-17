# Startup values used by the game facade without exposing the concrete policy.
# The frozen IPlayerPolicy protocol lives here too: it is a value contract over
# WorldState/TacticalBoard/PlayerControlSet, not a runtime policy.
add_library(football_ai_contracts INTERFACE)
add_library(football::ai_contracts ALIAS football_ai_contracts)
target_sources(football_ai_contracts INTERFACE FILE_SET HEADERS
  BASE_DIRS ${PROJECT_SOURCE_DIR}/include
  FILES
    ${PROJECT_SOURCE_DIR}/include/football/ai/ai_config.hpp
    ${PROJECT_SOURCE_DIR}/include/football/ai/tactical_board.hpp
    ${PROJECT_SOURCE_DIR}/include/football/ai/tactical_plan.hpp
    ${PROJECT_SOURCE_DIR}/include/football/ai/tactical_interpreter.hpp
    ${PROJECT_SOURCE_DIR}/include/football/ai/player_policy.hpp)
target_compile_features(football_ai_contracts INTERFACE cxx_std_23)
target_link_libraries(football_ai_contracts INTERFACE
  football::foundation football::pitch football::sim_contracts)

# Value policy: no actor/runtime link or input ownership.
add_library(football_ai STATIC)
add_library(football::ai ALIAS football_ai)
target_sources(football_ai
  PRIVATE
    ai_engine.cpp ai_engine.hpp
    default_ai.cpp
    default_decisions.cpp default_decisions.hpp
    default_policy.cpp default_policy.hpp
    eliza/defense.cpp eliza/defense.hpp
    eliza/eliza_geometry.hpp
    eliza/eliza_observation.hpp
    eliza/eliza_intent.cpp eliza/eliza_intent.hpp
    eliza/eliza_parameters.hpp
    eliza/eliza_policy.cpp eliza/eliza_policy.hpp
    eliza/off_ball.cpp eliza/off_ball.hpp
    eliza/keeper.cpp eliza/keeper.hpp
    eliza/on_ball.cpp eliza/on_ball.hpp
    eliza/support_position.cpp eliza/support_position.hpp
    tactical_board.cpp
    tactical_plan.cpp
    tactical_interpreter.cpp
  PUBLIC FILE_SET HEADERS
    BASE_DIRS ${PROJECT_SOURCE_DIR}/include
    FILES ${PROJECT_SOURCE_DIR}/include/football/ai/default_ai.hpp)
target_link_libraries(football_ai PUBLIC football::sim_contracts football::ai_contracts)
football_configure_target(football_ai)
