# Command: pure continuous motion, locomotion, reachability, validation and
# pass solving. No Player, Contact or Execution dependency.
add_library(football_player_command STATIC
  player_motion.cpp
  player_locomotion.cpp
  reachability.cpp
  player_pass_solver.cpp)
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
    ${PROJECT_SOURCE_DIR}/include/football/player/command/player_pass_solver.hpp
    ${PROJECT_SOURCE_DIR}/include/football/player/command/player_body_facing.hpp
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
  ${CMAKE_CURRENT_SOURCE_DIR}
  "${_player_upper_layer_regex}|#include \"(football/)?player/(contact|execution)/|#include \"(football/)?contact/")
football_player_check_boundary(
  ${PROJECT_SOURCE_DIR}/include/football/player/command
  "${_player_upper_layer_regex}|#include \"(football/)?player/(contact|execution)/|#include \"(football/)?contact/")
