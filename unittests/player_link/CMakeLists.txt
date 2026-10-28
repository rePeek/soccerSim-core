# Independent Player interfaces: State archive, Command protocol, Execution
# archive and independent Contact. No smoke consumer adds src/ to its includes.
file(GLOB_RECURSE _football_player_impl_files
  ${PROJECT_SOURCE_DIR}/src/player/*.hpp
  ${PROJECT_SOURCE_DIR}/src/player/*.cpp)
foreach(_football_player_file ${_football_player_impl_files})
  file(READ "${_football_player_file}" _football_player_content)
  if(_football_player_content MATCHES "#include \"(event|observation|runtime|referee|ai|team)/")
    message(FATAL_ERROR
      "Player boundary violation: ${_football_player_file} includes an upper-layer header")
  endif()
endforeach()

add_executable(football_player_link_smoke player_link_smoke.cpp)
target_include_directories(football_player_link_smoke PRIVATE
  ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(football_player_link_smoke
  PRIVATE
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,football_player_execution>"
    football::player_command
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,football_contact>"
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,football_player_state>"
    football::animation
    football::ball
    football::pitch
    football::collision
    football::foundation
    BqLog)
football_configure_target(football_player_link_smoke)

add_test(NAME player_link_smoke COMMAND football_player_link_smoke)
set_tests_properties(player_link_smoke PROPERTIES
  LABELS "unit;player"
  TIMEOUT 30)

# Independent link closure for each lower library.
add_executable(football_player_state_link_smoke state_link_smoke.cpp)
target_link_libraries(football_player_state_link_smoke
  PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,football_player_state>")
football_configure_target(football_player_state_link_smoke)
add_test(NAME player_state_link_smoke COMMAND football_player_state_link_smoke)
set_tests_properties(player_state_link_smoke PROPERTIES
  LABELS "unit;player" TIMEOUT 30)

add_executable(football_player_command_link_smoke command_link_smoke.cpp)
target_link_libraries(football_player_command_link_smoke
  PRIVATE football::player_command)
football_configure_target(football_player_command_link_smoke)
add_test(NAME player_command_link_smoke COMMAND football_player_command_link_smoke)
set_tests_properties(player_command_link_smoke PROPERTIES
  LABELS "unit;player" TIMEOUT 30)

get_target_property(_command_type football_player_command TYPE)
get_target_property(_command_deps football_player_command INTERFACE_LINK_LIBRARIES)
if(NOT _command_type STREQUAL "INTERFACE_LIBRARY" OR
   NOT _command_deps STREQUAL "football::player_state")
  message(FATAL_ERROR "Command must remain a value-only protocol over State")
endif()

# Pure prediction must remain callable without constructing Player, loading
# animation assets or linking Runtime, even though its implementation is Execution.
add_executable(football_player_prediction_link_smoke prediction_link_smoke.cpp)
target_link_libraries(football_player_prediction_link_smoke PRIVATE football::player_execution)
football_configure_target(football_player_prediction_link_smoke)
add_test(NAME player_prediction_link_smoke COMMAND football_player_prediction_link_smoke)
set_tests_properties(player_prediction_link_smoke PROPERTIES
  LABELS "interface;player" TIMEOUT 30)

# Compile each actual input-contract header separately, not legacy solver paths.
set(_command_smoke_sources)
foreach(_name player_control player_control_set player_action_request player_command_validation)
  set(_source ${CMAKE_CURRENT_BINARY_DIR}/${_name}_header.cpp)
  file(GENERATE OUTPUT ${_source} CONTENT "#include \"football/player/command/${_name}.hpp\"\n")
  list(APPEND _command_smoke_sources ${_source})
endforeach()
add_library(football_player_command_header_smoke OBJECT ${_command_smoke_sources})
target_link_libraries(football_player_command_header_smoke PRIVATE football::player_command)
football_configure_target(football_player_command_header_smoke)

set(_prediction_smoke_sources)
foreach(_name player_motion player_motion_profile player_body_facing player_pass_solver)
  set(_source ${CMAKE_CURRENT_BINARY_DIR}/${_name}_header.cpp)
  file(GENERATE OUTPUT ${_source} CONTENT "#include \"football/player/execution/${_name}.hpp\"\n")
  list(APPEND _prediction_smoke_sources ${_source})
endforeach()
add_library(football_player_prediction_header_smoke OBJECT ${_prediction_smoke_sources})
target_link_libraries(football_player_prediction_header_smoke PRIVATE football::player_execution)
football_configure_target(football_player_prediction_header_smoke)
