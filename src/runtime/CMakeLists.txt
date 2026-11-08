# Runtime composition. The public contracts are declared in src/contracts and
# the Event/Observation capability modules own their own archives; Runtime only
# links them. Internal headers are reached through the PRIVATE src/ root, which
# is never exported to simcore consumers.
add_library(football_sim_domain STATIC)
add_library(football::sim ALIAS football_sim_domain)
target_include_directories(football_sim_domain PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_sources(football_sim_domain PRIVATE
  simulation.cpp simulation.hpp
  simulation_body_shadow.cpp simulation_active_touch_shadow.cpp
  simulation_prepared_tick.cpp
  simulation_contact.cpp player_contact.hpp
  simulation_baked_body.cpp
  simulation_rule_touch.cpp
  touch_evidence_classification.hpp
  clock.cpp clock.hpp
  play_state.hpp restart_context.hpp rule_command_sink.hpp
  restart_controller.cpp restart_controller.hpp
  runtime_tick_facts.hpp
  restart_planner.cpp restart_planner.hpp
  restart_placement.cpp restart_placement.hpp
  player_authorization_port.cpp player_authorization_port.hpp
  world_state_builder.cpp world_state_builder.hpp
  ../testing/simulation_access.hpp)
target_link_libraries(football_sim_domain
  PUBLIC football::sim_contracts
  PRIVATE
    football::player football::contact football::animation football::ball
    football::pitch football::referee football::event football::observation football::possession
    BqLog)
football_configure_target(football_sim_domain)
# Resource lookup belongs to runtime loading, never public value/policy contracts.
football_require_animation_catalog(football_sim_domain)
