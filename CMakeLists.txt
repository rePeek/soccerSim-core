cmake_minimum_required(VERSION 3.24)
project(gameplayfootball LANGUAGES CXX)
include(CTest)
# Optional local build acceleration: use ccache whenever the dev shell
# provides it. This is a silent no-op when ccache is absent, never a hard
# dependency. The launcher must be set before any target is defined.
find_program(FOOTBALL_CCACHE_PROGRAM ccache)
if(FOOTBALL_CCACHE_PROGRAM)
  set(CMAKE_CXX_COMPILER_LAUNCHER "${FOOTBALL_CCACHE_PROGRAM}"
      CACHE STRING "C++ compiler launcher (ccache when available)")
  set(CMAKE_C_COMPILER_LAUNCHER "${FOOTBALL_CCACHE_PROGRAM}"
      CACHE STRING "C compiler launcher (ccache when available)")
endif()

# Shared build policy only; each module owns its sources, exports and link edges.
function(football_configure_target tgt)
  # Include paths are owned by each target's declared dependencies (FILE_SET
  # HEADERS / INTERFACE include dirs), never injected globally here.
  target_compile_features(${tgt} PUBLIC cxx_std_23)
  set_target_properties(${tgt} PROPERTIES POSITION_INDEPENDENT_CODE ON)
  get_target_property(_type ${tgt} TYPE)
  if(_type STREQUAL "EXECUTABLE")
    # Keep configured executables at the build root.
    set_target_properties(${tgt} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
  endif()
endfunction()

# Domain modules must not depend on Runtime implementation headers or the
# Runtime archive. This is the executable form of the dependency direction:
# a module may include public contracts, never runtime/ or football::sim.
function(football_check_no_runtime module_root)
  file(GLOB_RECURSE _files
    "${module_root}/*.cpp" "${module_root}/*.hpp" "${module_root}/CMakeLists.txt")
  foreach(_file ${_files})
    file(READ "${_file}" _content)
    if(_content MATCHES "#include \"(football/)?runtime/")
      message(FATAL_ERROR
        "Runtime include forbidden in ${_file}")
    endif()
    if(_file MATCHES "CMakeLists\\.txt$" AND
       _content MATCHES "football_sim_domain|football::sim[^_]")
      message(FATAL_ERROR
        "Runtime archive dependency forbidden in ${_file}")
    endif()
  endforeach()
endfunction()

set(CPM_SOURCE_CACHE "${PROJECT_SOURCE_DIR}/.cache/CPM"
  CACHE PATH "CPM dependency source cache")
include(cmake/CPM.cmake)
include(cmake/bqlog.cmake)
include(cmake/nlohmann_json.cmake)
include(cmake/cgltf.cmake)
include(cmake/animation_assets.cmake)

add_subdirectory(src/foundation)

# ANIM-2 A12: the Animation module owns two archives, both declared in
# src/animation/CMakeLists.txt: football::animation_base (format/data/IO/base
# selector, shared with the offline baker) and football::animation (runtime
# match, playback, pose and touch algorithms).
add_subdirectory(src/animation)

# football::pitch owns the pitch description and the static physics scene
# (ground/posts/crossbars as geometry colliders). ball and player read it;
# pitch never depends on them. Declared before ball, which links it.
add_subdirectory(src/pitch)

add_subdirectory(src/ball)

# Contact uses Player State; Player Execution uses Contact. CMake resolves
# link targets after all sibling module declarations.
add_subdirectory(src/player)
add_subdirectory(src/contact)

# Team is a football-domain module like ball/player/pitch: Runtime composes it.
add_subdirectory(src/team)

# Possession relations and arbitration, independent of Runtime.
add_subdirectory(src/possession)

# Restart execution is a domain module; Referee awards, Restart executes.
add_subdirectory(src/restart)

# Runtime is optional so the build continuously proves the domain modules are
# independently buildable and testable without it.
option(FOOTBALL_BUILD_RUNTIME "Build match simulation runtime" ON)

# Public value contracts are independent of Runtime and declared before the
# modules that consume them.
add_subdirectory(src/contracts)

# The Runtime layer composes these independent modules; each owns its own
# build target. Declared before runtime so the dependency direction is
# visible in the top-level build.
add_subdirectory(src/referee)
add_subdirectory(src/event)
add_subdirectory(src/observation)
if(FOOTBALL_BUILD_RUNTIME)
  add_subdirectory(src/runtime)
endif()

add_subdirectory(src/ai)

foreach(_domain_dir
    foundation animation pitch ball player contact team possession restart
    referee event observation)
  football_check_no_runtime(${PROJECT_SOURCE_DIR}/src/${_domain_dir})
endforeach()


if(FOOTBALL_BUILD_RUNTIME)
# The core shared library, composed from the simulation modules.
add_library(simcore SHARED src/env.cpp)
add_library(football::simcore ALIAS simcore)
target_sources(simcore PUBLIC FILE_SET HEADERS
  BASE_DIRS ${PROJECT_SOURCE_DIR}/include
  FILES ${PROJECT_SOURCE_DIR}/include/football/env.hpp)
target_link_libraries(simcore
  PUBLIC football::sim_contracts football::ai_contracts football::ball
  PRIVATE football::ai BqLog
    # Keep the existing runtime exports, including unreferenced GetRoleFromString.
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,football_sim_domain>"
    # These modules were part of sim_domain before the split. Whole-archive
    # them so the .so keeps exporting the same unreferenced symbols.
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,football_event>"
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,football_observation>"
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,football_possession>"
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,football_restart>"
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,football_contact_actor>"
    # Preserve Player/Contact exports; Command is a header-only protocol.
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,football_player_execution>"
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,football_contact>"
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,football_player_state>")
# env.cpp includes the internal runtime/simulation.hpp; the src/ root is a
# PRIVATE implementation include, never exported to simcore consumers.
target_include_directories(simcore PRIVATE ${PROJECT_SOURCE_DIR}/src)
football_configure_target(simcore)
endif()

if(BUILD_TESTING)
  CPMAddPackage(NAME Catch2 GITHUB_REPOSITORY catchorg/Catch2 GIT_TAG v3.7.1
    OPTIONS "CATCH_BUILD_TESTING OFF")
  add_subdirectory(unittests)
endif()
