# Public match/domain value contracts. This target is independent of Runtime:
# it must be declared before any module that consumes it and must never link
# football_sim_domain or any Runtime implementation target.
#
# snapshot.hpp pulls animation/types.hpp, so the contract carries animation_base.
add_library(football_sim_contracts INTERFACE)
add_library(football::sim_contracts ALIAS football_sim_contracts)
target_sources(football_sim_contracts INTERFACE FILE_SET HEADERS
  BASE_DIRS ${PROJECT_SOURCE_DIR}/include
  FILES
    ${PROJECT_SOURCE_DIR}/include/football/match/game_mode.hpp
    ${PROJECT_SOURCE_DIR}/include/football/match/player_role.hpp
    ${PROJECT_SOURCE_DIR}/include/football/match/team_side.hpp
    ${PROJECT_SOURCE_DIR}/include/football/match/match_config.hpp
    ${PROJECT_SOURCE_DIR}/include/football/match/active_lineup.hpp
    ${PROJECT_SOURCE_DIR}/include/football/match/phase.hpp
    ${PROJECT_SOURCE_DIR}/include/football/match/result.hpp
    ${PROJECT_SOURCE_DIR}/include/football/match/simulation_config.hpp
    ${PROJECT_SOURCE_DIR}/include/football/observation/world_state.hpp
    ${PROJECT_SOURCE_DIR}/include/football/observation/snapshot.hpp
    ${PROJECT_SOURCE_DIR}/include/football/event/event.hpp
    ${PROJECT_SOURCE_DIR}/include/football/event/match_event.hpp
    ${PROJECT_SOURCE_DIR}/include/football/event/event_trajectory.hpp)
target_compile_features(football_sim_contracts INTERFACE cxx_std_23)
target_link_libraries(football_sim_contracts INTERFACE
  football::foundation football::pitch football::animation_base football::team
  football::player)
