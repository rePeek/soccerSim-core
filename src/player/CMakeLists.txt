# Player owns three STATIC libraries with one-way dependencies:
# execution -> command -> state. Execution may also use the independent Contact.
# Only execution owns the concrete Player; Runtime owns cross-actor adapters.

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

set(_player_upper_layer_regex "#include \"(football/)?(event|observation|runtime|referee|ai|team)/")

add_subdirectory(state)
add_subdirectory(command)
add_subdirectory(execution)

add_library(football_player INTERFACE)
add_library(football::player ALIAS football_player)
target_link_libraries(football_player INTERFACE
  football_player_state
  football_player_command
  football_player_execution)
