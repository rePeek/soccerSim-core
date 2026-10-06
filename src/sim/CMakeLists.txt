# Exact value-only exports. Sharing sim/ does not expose actor runtime to AI.
add_library(football_sim_contracts INTERFACE)
add_library(football::sim_contracts ALIAS football_sim_contracts)
target_sources(football_sim_contracts INTERFACE FILE_SET HEADERS
  BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/..
  FILES player_control.hpp player_control_set.hpp world_state.hpp observation_epoch.hpp
    match_options.hpp match_phase.hpp match_result.hpp)
target_compile_features(football_sim_contracts INTERFACE cxx_std_23)
target_link_libraries(football_sim_contracts INTERFACE
  football::foundation football::model)

add_subdirectory(animation)

# query/rules/player remain internal organization, not dependency units.
add_library(football_sim STATIC)
add_library(football::sim ALIAS football_sim)
target_sources(football_sim PRIVATE
  gamedefines.cpp
  player/humanoid/humanoid_utils.cpp
  player/humanoid/humanoidbase.cpp
  player/humanoid/humanoid.cpp
  player/player.cpp
  player/player_control_builder.cpp
  player/player_locomotion.cpp
  ball.cpp match.cpp match_world_state.cpp formation.cpp simulation.cpp referee.cpp
  ai_support/mentalimage.cpp
  query/player_query.cpp query/reachability.cpp
  rules/offside.cpp rules/restart_placement.cpp
  player/kick_targeting.cpp team.cpp

  gamedefines.hpp formation.hpp ball.hpp match.hpp match_world_state.hpp
  referee.hpp team.hpp value_history.hpp
  ai_support/mentalimage.hpp
  query/player_query.hpp query/reachability.hpp
  rules/offside.hpp rules/restart_placement.hpp
  player/humanoid/humanoidbase.hpp player/humanoid/humanoid.hpp
  player/humanoid/humanoid_utils.hpp
  player/player.hpp player/player_control_builder.hpp player/player_kinematics.hpp
  player/player_retain_anchor.hpp player/player_ground_collider.hpp
  player/player_body_collider.hpp player/player_body_facing.hpp
  player/player_action.hpp player/player_action_executor.hpp player/player_action_volume.hpp
  player/player_locomotion.hpp player/player_decision_scheduler.hpp
  player/locomotion_intent_scheduler.hpp player/legacy_locomotion_command.hpp
  player/kick_targeting.hpp)
target_sources(football_sim PUBLIC FILE_SET HEADERS
  BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/..
  FILES simulation.hpp rng.hpp)
target_link_libraries(football_sim
  PUBLIC football::sim_contracts
  PRIVATE football::animation football::support football::model)
football_configure_target(football_sim)
