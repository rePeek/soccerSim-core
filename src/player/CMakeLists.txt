# Football Player is four compiled STATIC libraries with one-way dependencies:
#
#   execution -> command -> state
#   execution -> contact -> state
#   execution -> state
#
# Only execution owns the concrete Player actor. command/contact contain pure
# value-level computation; every Player-member definition and every adapter that
# needs a live Player lives in execution.
#
# The public include/football/player contract is carried by the execution
# target; the internal src/ root is exposed only because the four libraries
# share the same internal header paths during this migration.

# Configure-time library-boundary guards. A lower layer may include state and
# its own directory, never a sibling implementation directory or an upper layer.
function(football_player_check_boundary target_root forbidden_regex)
  file(GLOB_RECURSE _files
    ${target_root}/*.hpp
    ${target_root}/*.cpp)
  foreach(_file ${_files})
    file(READ "${_file}" _content)
    if(_content MATCHES "${forbidden_regex}")
      message(FATAL_ERROR
        "Player library boundary violation in ${_file}: matches ${forbidden_regex}")
    endif()
  endforeach()
endfunction()

set(_player_upper_layer_regex "#include \"(event|observation|runtime|referee|ai|team)/")

# ---------------------------------------------------------------------------
# State: value contracts and pure state utilities.
# ---------------------------------------------------------------------------
add_library(football_player_state STATIC
  state/role_parser.cpp)
add_library(football::player_state ALIAS football_player_state)
target_include_directories(football_player_state
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
  PRIVATE
    ${PROJECT_SOURCE_DIR}/src)
target_sources(football_player_state PUBLIC FILE_SET HEADERS
  BASE_DIRS ${PROJECT_SOURCE_DIR}/include
  FILES
    ${PROJECT_SOURCE_DIR}/include/football/player/state/player_definition.hpp
    ${PROJECT_SOURCE_DIR}/include/football/player/state/player_role.hpp
    ${PROJECT_SOURCE_DIR}/include/football/player/state/formation_entry.hpp
    ${PROJECT_SOURCE_DIR}/include/football/player/state/player_kinematics.hpp
    ${PROJECT_SOURCE_DIR}/include/football/player/state/player_action.hpp
    ${PROJECT_SOURCE_DIR}/include/football/player/state/player_body_profile.hpp
    ${PROJECT_SOURCE_DIR}/include/football/player/state/player_command_status.hpp
    ${PROJECT_SOURCE_DIR}/include/football/player/player_definition.hpp
    ${PROJECT_SOURCE_DIR}/include/football/player/player_role.hpp
    ${PROJECT_SOURCE_DIR}/include/football/player/formation_entry.hpp)
target_link_libraries(football_player_state
  PUBLIC
    football::foundation
    football::animation_base)
target_compile_features(football_player_state PUBLIC cxx_std_23)
football_configure_target(football_player_state)
football_player_check_boundary(
  ${PROJECT_SOURCE_DIR}/src/player/state
  "${_player_upper_layer_regex}|#include \"player/(command|contact|execution)/")

# ---------------------------------------------------------------------------
# Command: pure continuous motion, locomotion and reachability. No Player.
# ---------------------------------------------------------------------------
add_library(football_player_command STATIC
  command/player_motion.cpp
  command/player_locomotion.cpp
  command/reachability.cpp
  command/player_pass_solver.cpp)
add_library(football::player_command ALIAS football_player_command)
target_include_directories(football_player_command
  PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_sources(football_player_command PUBLIC FILE_SET HEADERS
  BASE_DIRS ${PROJECT_SOURCE_DIR}/include
  FILES
    ${PROJECT_SOURCE_DIR}/include/football/player/command/player_control.hpp
    ${PROJECT_SOURCE_DIR}/include/football/player/command/player_control_set.hpp
    ${PROJECT_SOURCE_DIR}/include/football/player/command/player_motion.hpp
    ${PROJECT_SOURCE_DIR}/include/football/player/command/player_motion_profile.hpp
    ${PROJECT_SOURCE_DIR}/include/football/player/command/player_body_facing.hpp
    ${PROJECT_SOURCE_DIR}/include/football/player/command/player_pass_solver.hpp
    ${PROJECT_SOURCE_DIR}/include/football/player/command/player_command_validation.hpp
    ${PROJECT_SOURCE_DIR}/include/football/player/player_control.hpp
    ${PROJECT_SOURCE_DIR}/include/football/player/player_control_set.hpp)
target_link_libraries(football_player_command
  PUBLIC
    football::player_state
    football::ball)
target_compile_features(football_player_command PUBLIC cxx_std_23)
football_configure_target(football_player_command)
football_player_check_boundary(
  ${PROJECT_SOURCE_DIR}/src/player/command
  "${_player_upper_layer_regex}|#include \"player/(contact|execution)/")

# ---------------------------------------------------------------------------
# Contact: pure touch proposal, impulse arbitration and geometry kernels.
# No Player, no Command dependency.
# ---------------------------------------------------------------------------
add_library(football_player_contact STATIC
  contact/player_active_touch_model.cpp
  contact/player_active_impulse.cpp
  contact/player_contact_kernel.cpp
  contact/player_touch_math_kernel.cpp)
add_library(football::player_contact ALIAS football_player_contact)
target_include_directories(football_player_contact
  PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_sources(football_player_contact PUBLIC FILE_SET HEADERS
  BASE_DIRS ${PROJECT_SOURCE_DIR}/include
  FILES
    ${PROJECT_SOURCE_DIR}/include/football/player/contact/foul_assessment.hpp
    ${PROJECT_SOURCE_DIR}/include/football/player/contact/player_touch_type.hpp
    ${PROJECT_SOURCE_DIR}/include/football/player/contact/player_active_touch_model.hpp
    ${PROJECT_SOURCE_DIR}/include/football/player/contact/player_active_impulse.hpp
    ${PROJECT_SOURCE_DIR}/include/football/player/contact/player_body_collider.hpp
    ${PROJECT_SOURCE_DIR}/include/football/player/contact/player_ground_collider.hpp
    ${PROJECT_SOURCE_DIR}/include/football/player/contact/player_contact_kernel.hpp
    ${PROJECT_SOURCE_DIR}/include/football/player/contact/player_touch_math_kernel.hpp
    ${PROJECT_SOURCE_DIR}/include/football/player/contact/player_body_collider_motion.hpp
    ${PROJECT_SOURCE_DIR}/include/football/player/foul_assessment.hpp
    ${PROJECT_SOURCE_DIR}/include/football/player/player_touch_type.hpp)
target_link_libraries(football_player_contact
  PUBLIC
    football::player_state
    football::ball
    football::geometry)
target_compile_features(football_player_contact PUBLIC cxx_std_23)
football_configure_target(football_player_contact)
football_player_check_boundary(
  ${PROJECT_SOURCE_DIR}/src/player/contact
  "${_player_upper_layer_regex}|#include \"player/(command|execution)/")

# ---------------------------------------------------------------------------
# Execution: the concrete Player actor, tick orchestration and every adapter
# that needs a live Player or an animation runtime.
# ---------------------------------------------------------------------------
add_library(football_player_execution STATIC
  execution/player_command_execution.cpp
  execution/player_control_builder.cpp
  execution/kick_targeting.cpp
  execution/player_action_motion.cpp
  execution/player_action_selection.cpp
  execution/player_anim_selection.cpp
  execution/player_animation_execution.cpp
  execution/player_ball_contact.cpp
  execution/player_contact.cpp
  execution/player_touch_execution.cpp
  execution/player_touch_math.cpp
  execution/player_touch_query.cpp
  execution/player_diagnostics.cpp
  execution/player_team_view.cpp
  execution/roster_query.cpp
  player.cpp
  player_tick.cpp)
add_library(football::player_execution ALIAS football_player_execution)
target_include_directories(football_player_execution
  PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_sources(football_player_execution PUBLIC FILE_SET HEADERS
  BASE_DIRS ${PROJECT_SOURCE_DIR}/include
  FILES
    ${PROJECT_SOURCE_DIR}/include/football/player/execution/player_touch_facts.hpp
    ${PROJECT_SOURCE_DIR}/include/football/player/execution/player_team_match_facts.hpp
    ${PROJECT_SOURCE_DIR}/include/football/player/execution/player_restart_facts.hpp
    ${PROJECT_SOURCE_DIR}/include/football/player/execution/player_action_feedback.hpp
    ${PROJECT_SOURCE_DIR}/include/football/player/player_touch_facts.hpp
    ${PROJECT_SOURCE_DIR}/include/football/player/player_team_match_facts.hpp
    ${PROJECT_SOURCE_DIR}/include/football/player/player_restart_facts.hpp)
target_link_libraries(football_player_execution
  PUBLIC
    football::player_state
    football::player_command
  PRIVATE
    football::player_contact
    football::geometry football::pitch football::ball football::animation
    football::collision BqLog)
target_compile_features(football_player_execution PUBLIC cxx_std_23)
football_configure_target(football_player_execution)
football_player_check_boundary(
  ${PROJECT_SOURCE_DIR}/src/player/execution
  "${_player_upper_layer_regex}")
# Root Player files are execution sources too.
foreach(_root_file
    ${PROJECT_SOURCE_DIR}/src/player/player.cpp
    ${PROJECT_SOURCE_DIR}/src/player/player.hpp
    ${PROJECT_SOURCE_DIR}/src/player/player_tick.cpp)
  file(READ "${_root_file}" _content)
  if(_content MATCHES "${_player_upper_layer_regex}")
    message(FATAL_ERROR
      "Player library boundary violation in ${_root_file}: upper-layer include")
  endif()
endforeach()

# Compatibility aggregate. It is an INTERFACE target, not a fifth library.
add_library(football_player INTERFACE)
add_library(football::player ALIAS football_player)
target_link_libraries(football_player INTERFACE
  football_player_state
  football_player_command
  football_player_contact
  football_player_execution)
