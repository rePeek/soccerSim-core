# Execution owns control realization, motion/prediction, pass solving, action
# scheduling and animation adapters. Cross-actor contacts belong to Runtime.
add_library(football_player_execution STATIC
  player_motion.cpp
  player_locomotion.cpp
  reachability.cpp
  player_pass_solver.cpp
  player_command_execution.cpp
  player_control_builder.cpp
  kick_targeting.cpp
  player_action_motion.cpp
  player_action_selection.cpp
  player_anim_selection.cpp
  player_animation_execution.cpp
  player_touch_execution.cpp
  player_touch_math.cpp
  player_touch_math_kernel.cpp
  player_touch_query.cpp
  player_diagnostics.cpp
  player_team_view.cpp
  roster_query.cpp
  player_execution.cpp
  player_execution_compat.cpp
  ${PROJECT_SOURCE_DIR}/src/player/player.cpp)
add_library(football::player_execution ALIAS football_player_execution)
target_include_directories(football_player_execution
  PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_sources(football_player_execution PUBLIC FILE_SET HEADERS
  BASE_DIRS ${PROJECT_SOURCE_DIR}/include
  FILES
    ${PROJECT_SOURCE_DIR}/include/football/player/execution/player_motion.hpp
    ${PROJECT_SOURCE_DIR}/include/football/player/execution/player_motion_profile.hpp
    ${PROJECT_SOURCE_DIR}/include/football/player/execution/player_body_facing.hpp
    ${PROJECT_SOURCE_DIR}/include/football/player/execution/player_pass_solver.hpp
    ${PROJECT_SOURCE_DIR}/include/football/player/execution/player_touch_facts.hpp
    ${PROJECT_SOURCE_DIR}/include/football/player/execution/player_team_match_facts.hpp
    ${PROJECT_SOURCE_DIR}/include/football/player/execution/player_restart_facts.hpp
    ${PROJECT_SOURCE_DIR}/include/football/player/execution/player_action_feedback.hpp
    ${PROJECT_SOURCE_DIR}/include/football/player/execution/player_touch_math_kernel.hpp)
target_link_libraries(football_player_execution
  PUBLIC
    football::player_state
    football::player_command
  PRIVATE
    football::contact
    football::geometry football::pitch football::ball football::animation
    football::collision BqLog)
target_compile_features(football_player_execution PUBLIC cxx_std_23)
football_configure_target(football_player_execution)
football_player_check_boundary(
  ${CMAKE_CURRENT_SOURCE_DIR}
  "${_player_upper_layer_regex}")
football_player_check_boundary(
  ${PROJECT_SOURCE_DIR}/include/football/player/execution
  "${_player_upper_layer_regex}")
foreach(_root_file
    ${PROJECT_SOURCE_DIR}/src/player/player.cpp
    ${PROJECT_SOURCE_DIR}/src/player/player.hpp)
  file(READ "${_root_file}" _content)
  if(_content MATCHES "${_player_upper_layer_regex}")
    message(FATAL_ERROR
      "Player library boundary violation in ${_root_file}: upper-layer include")
  endif()
endforeach()
