# Exact value-only exports; domain placement does not expose actor runtime to AI.
add_library(football_sim_contracts INTERFACE)
add_library(football::sim_contracts ALIAS football_sim_contracts)
target_sources(football_sim_contracts INTERFACE FILE_SET HEADERS
  BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/..
  FILES player/player_control.hpp player/player_control_set.hpp
    observation/world_state.hpp
  simulation_config.hpp runtime/phase.hpp runtime/result.hpp)
target_compile_features(football_sim_contracts INTERFACE cxx_std_23)
target_link_libraries(football_sim_contracts INTERFACE
  football::foundation football::model)

add_subdirectory(animation)
add_subdirectory(ball)

# Domain directories organize concepts, not public/private dependency units.
add_library(football_sim STATIC)
add_library(football::sim ALIAS football_sim)
target_sources(football_sim PRIVATE
  simulation.cpp simulation_body_shadow.cpp simulation_active_touch_shadow.cpp
  simulation_prepared_tick.cpp player/player_touch_preparation.hpp
  player/player_active_touch_shadow.hpp player/player_active_impulse.hpp
  player/player_active_touch_model.hpp
  touch_evidence_classification.hpp
  event/touch_state.hpp event/touch_type.hpp
  event/touch_record.hpp
  event/event.hpp event/event_transition.hpp
  event/event_recognizer.cpp event/event_recognizer.hpp
  event/event_trajectory.hpp
  event/accepted_touch.hpp event/accepted_touch_sink.hpp
  referee/ruling.hpp event/match_event.hpp event/event_log.hpp
  event/touch_query.cpp event/touch_query.hpp
  runtime/clock.cpp runtime/clock.hpp
  team/team.cpp team/team.hpp team/formation.cpp team/formation.hpp
  team/formation_roles.cpp team/formation_entry.hpp
  team/possession.cpp team/possession.hpp
  event/touch_type.hpp
  ball_touch_application.cpp ball_touch_application.hpp
  observation/world_state_builder.cpp observation/world_state_builder.hpp
  observation/pitch_frame.cpp observation/pitch_frame.hpp
  observation/mentalimage.cpp observation/mentalimage.hpp observation/player_image.hpp
  observation/mentalimage_sampling.cpp observation/mentalimage_sampling.hpp
  observation/snapshot_history.cpp
  observation/temporal_graph.cpp
  observation/touch_inference.cpp observation/touch_inference.hpp
  query/player_query.cpp query/reachability.cpp
  query/player_query.hpp query/reachability.hpp query/force_spot.hpp
  referee/referee.cpp referee/referee.hpp
  referee/goal.cpp referee/goal.hpp
  referee/period.cpp referee/period.hpp
  referee/ball_rules.cpp referee/ball_rules.hpp
  referee/offside.cpp referee/restart_placement.cpp referee/restart_readiness.cpp
  referee/ball_touch_facts.hpp
  referee/referee_state.hpp referee/referee_tick_facts.hpp referee/rule_command_sink.hpp referee/referee_view.hpp
  testing/simulation_access.hpp
  referee/offside.hpp referee/restart_placement.hpp referee/restart_readiness.hpp
  player/player.cpp player/player_control_builder.cpp player/player_locomotion.cpp
  player/player_contact.cpp player/player_contact.hpp
  player/player_ball_contact.cpp player/player_ball_contact.hpp
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
  player/player_body_collider.hpp player/player_body_collider_motion.hpp
  player/player_body_collision_shadow.hpp player/player_body_facing.hpp
  player/player_body_contact_diagnostics.hpp player/player_body_pose_shadow.hpp
  player/player_action.hpp player/player_action_executor.hpp player/player_action_volume.hpp
  player/player_locomotion.hpp player/player_decision_scheduler.hpp
  player/locomotion_intent_scheduler.hpp player/legacy_locomotion_command.hpp
  player/kick_targeting.hpp)
target_sources(football_sim PUBLIC FILE_SET HEADERS
  BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/..
  FILES simulation.hpp observation/snapshot.hpp observation/snapshot_history.hpp
    observation/temporal_graph.hpp event/event_trajectory.hpp player/foul_assessment.hpp
    observation/player_slots.hpp)
target_link_libraries(football_sim
  PUBLIC football::sim_contracts
  PRIVATE football::animation football::model football::ball BqLog)
football_configure_target(football_sim)
# Resource lookup belongs to runtime loading, never public value/policy contracts.
target_compile_definitions(football_sim PRIVATE
  GFOOTBALL_BAKED_ANIM_PATH="${PROJECT_SOURCE_DIR}/assets/runtime/animations.simanim")
