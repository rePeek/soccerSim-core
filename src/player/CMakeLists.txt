# Football Player is four compiled STATIC libraries with one-way dependencies:
#
#   execution -> command -> state
#   execution -> contact -> state
#   execution -> state
#
# Only execution owns the concrete Player actor. Each responsibility directory
# owns its target, sources and public FILE_SET in its own CMakeLists.txt.
#
# The public include/football/player contract is carried by the four library
# file sets; the internal src/ root is PRIVATE to each library during this
# migration.

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

add_subdirectory(state)
add_subdirectory(command)
add_subdirectory(contact)
add_subdirectory(execution)

# Compatibility aggregate. It is an INTERFACE target, not a fifth library.
add_library(football_player INTERFACE)
add_library(football::player ALIAS football_player)
target_link_libraries(football_player INTERFACE
  football_player_state
  football_player_command
  football_player_contact
  football_player_execution)
