# Independent Player module boundary: four compiled libraries with one-way
# dependencies. Each smoke target links only the layer under test plus its
# declared base dependencies, never a sibling implementation layer or Runtime.
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
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,football_player_command>"
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,football_player_contact>"
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
  PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,football_player_command>")
football_configure_target(football_player_command_link_smoke)
add_test(NAME player_command_link_smoke COMMAND football_player_command_link_smoke)
set_tests_properties(player_command_link_smoke PROPERTIES
  LABELS "unit;player" TIMEOUT 30)

add_executable(football_player_contact_link_smoke contact_link_smoke.cpp)
target_link_libraries(football_player_contact_link_smoke
  PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,football_player_contact>")
football_configure_target(football_player_contact_link_smoke)
add_test(NAME player_contact_link_smoke COMMAND football_player_contact_link_smoke)
set_tests_properties(player_contact_link_smoke PROPERTIES
  LABELS "unit;player" TIMEOUT 30)
