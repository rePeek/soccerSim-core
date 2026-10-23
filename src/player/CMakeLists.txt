# Independent football::player module: actor state, locomotion, contact,
# selection, motion and the per-tick behaviour. The public Player value
# contracts live in include/football/player/.
#
# Dependency direction: foundation/geometry/pitch + ball/animation -> player.
# Player must never link Runtime/Referee/AI/Event/Observation implementation.
add_library(football_player STATIC
  kick_targeting.cpp
  perception.cpp
  player_action_motion.cpp
  player_action_selection.cpp
  player_anim_selection.cpp
  player_ball_contact.cpp
  player_contact.cpp
  player_control_builder.cpp
  player.cpp
  player_diagnostics.cpp
  player_locomotion.cpp
  player_motion.cpp
  player_team_view.cpp
  player_tick.cpp
  player_touch_execution.cpp
  player_touch_math.cpp
  player_touch_query.cpp
  reachability.cpp
  role_parser.cpp
  roster_query.cpp)
add_library(football::player ALIAS football_player)
target_sources(football_player PUBLIC FILE_SET HEADERS
  BASE_DIRS ${PROJECT_SOURCE_DIR}/include
  FILES
    ${PROJECT_SOURCE_DIR}/include/football/player/player_definition.hpp
    ${PROJECT_SOURCE_DIR}/include/football/player/foul_assessment.hpp
    ${PROJECT_SOURCE_DIR}/include/football/player/player_control.hpp
    ${PROJECT_SOURCE_DIR}/include/football/player/player_control_set.hpp
    ${PROJECT_SOURCE_DIR}/include/football/player/player_role.hpp
    ${PROJECT_SOURCE_DIR}/include/football/player/formation_entry.hpp
    ${PROJECT_SOURCE_DIR}/include/football/player/player_touch_type.hpp
    ${PROJECT_SOURCE_DIR}/include/football/player/player_touch_facts.hpp
    ${PROJECT_SOURCE_DIR}/include/football/player/player_team_match_facts.hpp
    ${PROJECT_SOURCE_DIR}/include/football/player/player_restart_facts.hpp)
target_include_directories(football_player
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
  PRIVATE
    ${PROJECT_SOURCE_DIR}/src)
target_compile_features(football_player PUBLIC cxx_std_23)

# Declared lower-layer closure. Player is self-contained: the boundary suite
# (unittests/player_link) force-links every object without Runtime/Referee/
# Event/Observation, and a configure-time guard rejects upper-layer includes.
target_link_libraries(football_player
  PUBLIC
    football::foundation football::geometry football::pitch
    football::ball football::animation
  PRIVATE
    football::collision BqLog)

football_configure_target(football_player)
