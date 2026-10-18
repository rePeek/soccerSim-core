# Independent Player module boundary.
#
# The executable includes only the public include tree (its include dir is
# explicitly ${PROJECT_SOURCE_DIR}/include, never src/) and force-links every
# football_player object. It links no sim/domain target, so any remaining
# Player -> Runtime/Referee/Event/Observation undefined symbol fails the link:
# the acceptance is the link closure, not the surface CMakeLists text.

# Configure-time boundary guard: no Player translation unit or internal header
# may include an upper-layer implementation header. This keeps the achieved
# closure from silently regressing.
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
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,football_player>"
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
