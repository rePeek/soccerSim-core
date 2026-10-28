# Command is a header-only input protocol. Execution consumes it; the protocol
# never invokes an actor, animation, contact or a motion/pass solver.
add_library(football_player_command INTERFACE)
add_library(football::player_command ALIAS football_player_command)
set(_player_command_headers
  ${PROJECT_SOURCE_DIR}/include/football/player/command/player_control.hpp
  ${PROJECT_SOURCE_DIR}/include/football/player/command/player_control_set.hpp
  ${PROJECT_SOURCE_DIR}/include/football/player/command/player_action_request.hpp
  ${PROJECT_SOURCE_DIR}/include/football/player/command/player_command_validation.hpp
  ${PROJECT_SOURCE_DIR}/include/football/player/player_control.hpp
  ${PROJECT_SOURCE_DIR}/include/football/player/player_control_set.hpp)
target_sources(football_player_command INTERFACE FILE_SET HEADERS
  BASE_DIRS ${PROJECT_SOURCE_DIR}/include
  FILES ${_player_command_headers})
target_link_libraries(football_player_command INTERFACE football::player_state)
target_compile_features(football_player_command INTERFACE cxx_std_23)

# Check the actual protocol, not the old motion-header compatibility forwarders
# in command/. Those forwarders are exported and owned by Execution instead.
foreach(_file ${_player_command_headers})
  file(READ "${_file}" _content)
  if(_content MATCHES "${_player_upper_layer_regex}|#include \"(football/)?player/(contact|execution)/|#include \"(football/)?(contact|ball|animation)/|#include \"player/player.hpp\"")
    message(FATAL_ERROR "Command protocol boundary violation in ${_file}")
  endif()
endforeach()
