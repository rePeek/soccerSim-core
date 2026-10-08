# Exact value-only exports; domain placement does not expose actor runtime to AI.
add_library(football_sim_contracts INTERFACE)
add_library(football::sim_contracts ALIAS football_sim_contracts)
target_sources(football_sim_contracts INTERFACE FILE_SET HEADERS
  BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/..
  FILES player/player_control.hpp player/player_control_set.hpp
    observation/world_state.hpp
    simulation_config.hpp rules/phase.hpp rules/result.hpp
    time/tick.hpp time/tick_boundary.hpp)
target_compile_features(football_sim_contracts INTERFACE cxx_std_23)
target_link_libraries(football_sim_contracts INTERFACE
  football::foundation football::model)

add_subdirectory(animation)

# Domain directories organize concepts, not public/private dependency units.
add_library(football_sim STATIC)
add_library(football::sim ALIAS football_sim)
target_sources(football_sim PRIVATE
  simulation.cpp
  pitch_geometry.hpp
  event/ball_touch.hpp event/ball_touch_sink.hpp event/touch_state.hpp
  event/ball_touch_dispatcher.cpp event/ball_touch_dispatcher.hpp
  event/touch_query.cpp event/touch_query.hpp
  rules/clock.cpp rules/clock.hpp
  team/team.cpp team/team.hpp team/formation.cpp team/formation.hpp
  team/formation_roles.cpp team/formation_entry.hpp
  team/possession.cpp team/possession.hpp
  ball/ball.cpp ball/ball.hpp ball/ball_timing.hpp ball/ball_touch.hpp
  ball/ball_environment.hpp ball/ball_player_contact.cpp ball/ball_player_contact.hpp
  ball/ball_touch_application.cpp ball/ball_touch_application.hpp
  observation/world_state_builder.cpp observation/world_state_builder.hpp
  observation/pitch_frame.cpp observation/pitch_frame.hpp
  observation/mentalimage.cpp observation/mentalimage.hpp observation/player_image.hpp
  observation/mentalimage_sampling.cpp observation/mentalimage_sampling.hpp
  query/player_query.cpp query/reachability.cpp
  query/player_query.hpp query/reachability.hpp query/force_spot.hpp
  rules/referee.cpp rules/referee.hpp
  rules/goal.cpp rules/goal.hpp
  rules/period.cpp rules/period.hpp
  rules/offside.cpp rules/restart_placement.cpp rules/restart_readiness.cpp
  rules/ball_touch_facts.hpp
  rules/referee_tick_facts.hpp rules/rule_command_sink.hpp
  testing/simulation_access.hpp
  rules/offside.hpp rules/restart_placement.hpp rules/restart_readiness.hpp
  player/player.cpp player/player_control_builder.cpp player/player_locomotion.cpp
  player/player_contact.cpp player/player_contact.hpp
  player/possession.cpp player/possession.hpp
  player/kick_targeting.cpp
  player/humanoid/humanoid_utils.cpp
  player/humanoid/humanoidbase.cpp
  player/humanoid/humanoid.cpp
  player/humanoid/humanoidbase.hpp player/humanoid/humanoid.hpp
  player/humanoid/humanoid_utils.hpp
  player/player.hpp player/player_control_builder.hpp player/player_kinematics.hpp
  player/player_command.hpp player/player_motion_constants.hpp
  player/player_tick_context.hpp
  player/player_retain_anchor.hpp player/player_ground_collider.hpp
  player/player_body_collider.hpp player/player_body_facing.hpp
  player/player_action.hpp player/player_action_executor.hpp player/player_action_volume.hpp
  player/player_locomotion.hpp player/player_decision_scheduler.hpp
  player/locomotion_intent_scheduler.hpp player/legacy_locomotion_command.hpp
  player/kick_targeting.hpp)
target_sources(football_sim PUBLIC FILE_SET HEADERS
  BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/..
  FILES simulation.hpp random/rng.hpp)
target_link_libraries(football_sim
  PUBLIC football::sim_contracts
  PRIVATE football::animation football::model BqLog)
football_configure_target(football_sim)
# Resource lookup belongs to runtime loading, never public value/policy contracts.
target_compile_definitions(football_sim PRIVATE
  GFOOTBALL_BAKED_ANIM_PATH="${PROJECT_SOURCE_DIR}/assets/runtime/animations.simanim")
