# Player model and input contracts are flat. Their link boundaries remain
# separate from Execution, which consumes them and may use Contact.
function(football_player_check_files forbidden_regex)
  foreach(_file ${ARGN})
    file(READ "${_file}" _content)
    if(_content MATCHES "${forbidden_regex}")
      message(FATAL_ERROR
        "Player library boundary violation in ${_file}: matches ${forbidden_regex}")
    endif()
  endforeach()
endfunction()

function(football_player_check_boundary target_root forbidden_regex)
  file(GLOB_RECURSE _files ${target_root}/*.hpp ${target_root}/*.cpp)
  football_player_check_files("${forbidden_regex}" ${_files})
endfunction()

# Flat paths do not imply shared ownership: a model must not include an input
# or execution contract, and an input must not include an execution contract.
function(football_player_check_contract_files forbidden_regex allowed_headers)
  football_player_check_files("${forbidden_regex}" ${ARGN})
  foreach(_file ${ARGN})
    file(READ "${_file}" _content)
    string(REGEX MATCHALL "#include \"football/player/[^\"]+\"" _includes "${_content}")
    foreach(_include ${_includes})
      string(REGEX REPLACE "#include \"([^\"]+)\"" "${PROJECT_SOURCE_DIR}/include/\\1" _path "${_include}")
      if(NOT _path IN_LIST allowed_headers)
        message(FATAL_ERROR "Player contract boundary violation in ${_file}: ${_include}")
      endif()
    endforeach()
  endforeach()
endfunction()

set(_player_upper_layer_regex "#include \"(football/)?(event|observation|runtime|referee|ai|team)/")

# State is a link boundary, not a directory. Never recursively scan Player here:
# its root also contains the concrete actor and private execution-facing types.
set(_player_model_headers
  ${PROJECT_SOURCE_DIR}/include/football/player/player_definition.hpp
  ${PROJECT_SOURCE_DIR}/include/football/player/player_role.hpp
  ${PROJECT_SOURCE_DIR}/include/football/player/formation_entry.hpp
  ${PROJECT_SOURCE_DIR}/include/football/player/player_kinematics.hpp
  ${PROJECT_SOURCE_DIR}/include/football/player/player_action.hpp
  ${PROJECT_SOURCE_DIR}/include/football/player/player_body_profile.hpp
  ${PROJECT_SOURCE_DIR}/include/football/player/player_command_status.hpp)
add_library(football_player_state STATIC role_parser.cpp)
add_library(football::player_state ALIAS football_player_state)
target_include_directories(football_player_state
  PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
  PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_sources(football_player_state PUBLIC FILE_SET HEADERS
  BASE_DIRS ${PROJECT_SOURCE_DIR}/include
  FILES ${_player_model_headers})
target_link_libraries(football_player_state PUBLIC
  football::foundation football::animation_base)
football_configure_target(football_player_state)
football_player_check_contract_files(
  "${_player_upper_layer_regex}|#include \"(football/)?player/(command|contact|execution)/|#include \"(football/)?contact/|#include \"player/player.hpp\""
  "${_player_model_headers}"
  ${_player_model_headers}
  ${CMAKE_CURRENT_SOURCE_DIR}/role_parser.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/role_parser.cpp)

# Header-only input protocol; Execution consumes it, never the reverse.
add_library(football_player_command INTERFACE)
add_library(football::player_command ALIAS football_player_command)
set(_player_command_headers
  ${PROJECT_SOURCE_DIR}/include/football/player/player_control.hpp
  ${PROJECT_SOURCE_DIR}/include/football/player/player_control_set.hpp
  ${PROJECT_SOURCE_DIR}/include/football/player/player_action_request.hpp
  ${PROJECT_SOURCE_DIR}/include/football/player/player_command_validation.hpp)
target_sources(football_player_command INTERFACE FILE_SET HEADERS
  BASE_DIRS ${PROJECT_SOURCE_DIR}/include
  FILES ${_player_command_headers})
target_link_libraries(football_player_command INTERFACE football::player_state)
target_compile_features(football_player_command INTERFACE cxx_std_23)
football_player_check_contract_files(
  "${_player_upper_layer_regex}|#include \"(football/)?player/(contact|execution)/|#include \"(football/)?(contact|ball|animation)/|#include \"player/player.hpp\""
  "${_player_model_headers};${_player_command_headers}"
  ${_player_command_headers})

add_subdirectory(execution)

add_library(football_player INTERFACE)
add_library(football::player ALIAS football_player)
target_link_libraries(football_player INTERFACE
  football_player_state football_player_command football_player_execution)
