# Independent Team module boundary.
#
# Mirrors the Player boundary suite: the executable includes only the public
# include tree, force-links every football_team object, and links only Team's
# declared closure (player/foundation/pitch + Player's lower layers). It never
# links Runtime/Referee/Event/AI, so any undeclared Team -> upper-layer symbol
# makes the link fail.

# Configure-time guard: no Team source may include an upper-layer implementation
# header. Team legitimately includes Player's internal header (it owns the
# concrete Player objects), so `player/` is allowed.
file(GLOB_RECURSE _football_team_impl_files
  ${PROJECT_SOURCE_DIR}/src/team/*.hpp
  ${PROJECT_SOURCE_DIR}/src/team/*.cpp)
foreach(_football_team_file ${_football_team_impl_files})
  file(READ "${_football_team_file}" _football_team_content)
  if(_football_team_content MATCHES "#include \"(event|observation|runtime|referee|ai)/")
    message(FATAL_ERROR
      "Team boundary violation: ${_football_team_file} includes an upper-layer header")
  endif()
endforeach()

add_executable(football_team_link_smoke team_link_smoke.cpp)
target_include_directories(football_team_link_smoke PRIVATE
  ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(football_team_link_smoke
  PRIVATE
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,football_team>"
    football::animation
    football::ball
    football::pitch
    football::collision
    football::foundation
    BqLog)
football_configure_target(football_team_link_smoke)

add_test(NAME team_link_smoke COMMAND football_team_link_smoke)
set_tests_properties(team_link_smoke PROPERTIES
  LABELS "unit;team"
  TIMEOUT 30)
