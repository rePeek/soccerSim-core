# Exact value-only exports; domain placement does not expose actor runtime to AI.
# The public value types live in include/football/ so a consumer can include
# football/env.hpp without the src/ include root. snapshot.hpp pulls
# animation/types.hpp, so the contract target carries animation_base.
add_library(football_sim_contracts INTERFACE)
add_library(football::sim_contracts ALIAS football_sim_contracts)
target_sources(football_sim_contracts INTERFACE FILE_SET HEADERS
  BASE_DIRS ${PROJECT_SOURCE_DIR}/include
  FILES
    ${PROJECT_SOURCE_DIR}/include/football/player/player_definition.hpp
    ${PROJECT_SOURCE_DIR}/include/football/player/player_control.hpp
    ${PROJECT_SOURCE_DIR}/include/football/player/player_control_set.hpp
    ${PROJECT_SOURCE_DIR}/include/football/player/player_role.hpp
    ${PROJECT_SOURCE_DIR}/include/football/player/formation_entry.hpp
    ${PROJECT_SOURCE_DIR}/include/football/referee/game_mode.hpp
    ${PROJECT_SOURCE_DIR}/include/football/runtime/formation.hpp
    ${PROJECT_SOURCE_DIR}/include/football/runtime/match_config.hpp
    ${PROJECT_SOURCE_DIR}/include/football/runtime/team.hpp
    ${PROJECT_SOURCE_DIR}/include/football/runtime/team_lineup.hpp
    ${PROJECT_SOURCE_DIR}/include/football/runtime/team_roles.hpp
    ${PROJECT_SOURCE_DIR}/include/football/runtime/formation_preset.hpp
    ${PROJECT_SOURCE_DIR}/include/football/runtime/phase.hpp
    ${PROJECT_SOURCE_DIR}/include/football/runtime/result.hpp
    ${PROJECT_SOURCE_DIR}/include/football/runtime/simulation_config.hpp
    ${PROJECT_SOURCE_DIR}/include/football/observation/world_state.hpp
    ${PROJECT_SOURCE_DIR}/include/football/observation/snapshot.hpp
    ${PROJECT_SOURCE_DIR}/include/football/event/event.hpp
    ${PROJECT_SOURCE_DIR}/include/football/event/event_trajectory.hpp)
target_compile_features(football_sim_contracts INTERFACE cxx_std_23)
target_link_libraries(football_sim_contracts INTERFACE
  football::foundation football::pitch football::animation_base)


# The domain target is declared here (the Runtime layer) and collects sources
# from the sibling module CMakeLists through target_sources(PRIVATE ...). The
# internal headers are no longer exported: the domain implementation reaches
# them through the shared src/ root as runtime/..., player/..., etc. That root
# is PRIVATE, so libsimcore consumers only see include/football/.
add_library(football_sim_domain STATIC)
add_library(football::sim ALIAS football_sim_domain)
target_include_directories(football_sim_domain PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_sources(football_sim_domain PRIVATE
  simulation.cpp simulation.hpp
  simulation_body_shadow.cpp simulation_active_touch_shadow.cpp
  simulation_prepared_tick.cpp
  simulation_baked_body.cpp
  simulation_rule_touch.cpp
  touch_evidence_classification.hpp
  clock.cpp clock.hpp roster.cpp roster.hpp
  formation.cpp formation_build.hpp
  team_validation.cpp
  team_state.hpp team_view.hpp team_facts.hpp
  possession.cpp possession.hpp
  roster_possession.cpp roster_possession.hpp
  ../testing/simulation_access.hpp)
target_link_libraries(football_sim_domain
  PUBLIC football::sim_contracts
  PRIVATE football::animation football::ball football::pitch BqLog)
football_configure_target(football_sim_domain)
# Resource lookup belongs to runtime loading, never public value/policy contracts.
target_compile_definitions(football_sim_domain PRIVATE
  GFOOTBALL_BAKED_ANIM_PATH="${PROJECT_SOURCE_DIR}/assets/runtime/animations.simanim")
