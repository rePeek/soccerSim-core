# State: value contracts and pure state utilities.
add_library(football_player_state STATIC
  role_parser.cpp)
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
  ${CMAKE_CURRENT_SOURCE_DIR}
  "${_player_upper_layer_regex}|#include \"(football/)?player/(command|contact|execution)/|#include \"(football/)?contact/")
football_player_check_boundary(
  ${PROJECT_SOURCE_DIR}/include/football/player/state
  "${_player_upper_layer_regex}|#include \"(football/)?player/(command|contact|execution)/|#include \"(football/)?contact/")
