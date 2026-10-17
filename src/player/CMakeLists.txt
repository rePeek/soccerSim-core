# Player module implementation. Declares only its sources on the shared
# football_sim_domain target; the target itself (and the public value
# contracts) are declared by the Runtime layer. Public Player value types live
# in include/football/player/; player.hpp and the rest stay internal.
target_sources(football_sim_domain PRIVATE
  foul_assessment.hpp
  kick_targeting.cpp kick_targeting.hpp
  legacy_locomotion_command.hpp
  locomotion_animation_strategy.hpp
  locomotion_intent_scheduler.hpp
  perception.cpp perception.hpp
  player_action_execution_state.hpp
  player_action_executor.hpp
  player_action.hpp
  player_action_motion.cpp
  player_action_request.hpp
  player_action_selection.cpp
  player_action_volume.hpp
  player_active_impulse.hpp
  player_active_touch_model.hpp
  player_active_touch_shadow.hpp
  player_anim_selection.cpp
  player_baked_body_pose.hpp
  player_baked_contact_regions.hpp
  player_baked_pose_realization.hpp
  player_baked_pose_sample.hpp
  player_ball_contact.cpp player_ball_contact.hpp
  player_body_collider.hpp
  player_body_collider_motion.hpp
  player_body_collision_shadow.hpp
  player_body_contact_diagnostics.hpp
  player_body_facing.hpp
  player_body_pose_shadow.hpp
  player_body_profile.hpp
  player_command.hpp
  player_contact.cpp player_contact.hpp
  player_control_builder.cpp player_control_builder.hpp
  player.cpp
  player_decision_scheduler.hpp
  player_diagnostics.cpp player_diagnostics.hpp
  player_ground_collider.hpp
  player.hpp
  player_interrupt.hpp
  player_kinematics.hpp
  player_locomotion_animation.hpp
  player_locomotion.cpp player_locomotion.hpp
  player_motion_constants.hpp
  player_motion.cpp player_motion.hpp player_motion_profile.hpp
  player_retain_anchor.hpp
  player_runtime_sink.hpp
  player_selected_action.hpp
  player_selection_decisions.hpp
  player_send_off_sink.hpp
  player_spatial_state.hpp
  player_team_view.cpp player_team_view.hpp
  player_tick_context.hpp
  player_tick.cpp
  player_touch_choice.hpp
  player_touch_execution.cpp
  player_touch_math.cpp player_touch_math.hpp
  player_touch_preparation.hpp
  reachability.cpp reachability.hpp
  role_parser.cpp role_parser.hpp
  roster_query.cpp roster_query.hpp)
