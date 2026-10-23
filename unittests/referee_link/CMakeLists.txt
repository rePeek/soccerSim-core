# Independent Referee module boundary.
#
# The value-only rule library must close over foundation + pitch alone: no
# Player, Event, Runtime, AI or Team symbol. This executable includes only the
# public include tree and force-links every football_referee object, so any
# undeclared Referee -> upper-layer undefined symbol fails the link.

# Configure-time guard: no Referee source may include a Player/Event/Runtime/AI/
# Team implementation header. Public football/... contracts are always allowed.
file(GLOB_RECURSE _football_referee_impl_files
  ${PROJECT_SOURCE_DIR}/src/referee/*.hpp
  ${PROJECT_SOURCE_DIR}/src/referee/*.cpp)
foreach(_football_referee_file ${_football_referee_impl_files})
  file(READ "${_football_referee_file}" _football_referee_content)
  if(_football_referee_content MATCHES "#include \"(event|observation|runtime|ai|team|player)/")
    message(FATAL_ERROR
      "Referee boundary violation: ${_football_referee_file} includes an upper-layer header")
  endif()
endforeach()

add_executable(football_referee_link_smoke referee_link_smoke.cpp)
target_include_directories(football_referee_link_smoke PRIVATE
  ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(football_referee_link_smoke
  PRIVATE
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,football_referee>"
    football::pitch
    football::foundation)
football_configure_target(football_referee_link_smoke)

add_test(NAME referee_link_smoke COMMAND football_referee_link_smoke)
set_tests_properties(referee_link_smoke PROPERTIES
  LABELS "unit;referee"
  TIMEOUT 30)
