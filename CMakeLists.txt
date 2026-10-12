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
    # Keep product/tool executables at the build root.
    set_target_properties(${tgt} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
  endif()
endfunction()

set(CPM_SOURCE_CACHE "${PROJECT_SOURCE_DIR}/.cache/CPM"
  CACHE PATH "CPM dependency source cache")
include(cmake/CPM.cmake)
include(cmake/bqlog.cmake)

add_subdirectory(src/foundation)

# Header-only modules have no implementation directory, so their INTERFACE
# targets are declared here; the headers live in include/football/<module>/.
add_library(football_animation INTERFACE)
add_library(football::animation ALIAS football_animation)
target_sources(football_animation INTERFACE FILE_SET HEADERS
  BASE_DIRS ${PROJECT_SOURCE_DIR}/include
  FILES
    ${PROJECT_SOURCE_DIR}/include/football/animation/types.hpp
    ${PROJECT_SOURCE_DIR}/include/football/animation/format.hpp
    ${PROJECT_SOURCE_DIR}/include/football/animation/clip.hpp
    ${PROJECT_SOURCE_DIR}/include/football/animation/library.hpp
    ${PROJECT_SOURCE_DIR}/include/football/animation/selector.hpp
    ${PROJECT_SOURCE_DIR}/include/football/animation/selection_math.hpp
    ${PROJECT_SOURCE_DIR}/include/football/animation/selection_query.hpp
    ${PROJECT_SOURCE_DIR}/include/football/animation/quadrant.hpp)
target_compile_features(football_animation INTERFACE cxx_std_23)
target_link_libraries(football_animation INTERFACE football::foundation)

add_subdirectory(src/ball)

# football::pitch owns the Pitch description; football::pitch_ball owns the
# header-only Pitch -> BallWorld assembly and pulls football::ball.
add_library(football_pitch INTERFACE)
add_library(football::pitch ALIAS football_pitch)
target_sources(football_pitch INTERFACE FILE_SET HEADERS
  BASE_DIRS ${PROJECT_SOURCE_DIR}/include
  FILES ${PROJECT_SOURCE_DIR}/include/football/pitch/pitch.hpp)
target_compile_features(football_pitch INTERFACE cxx_std_23)
target_link_libraries(football_pitch INTERFACE football::foundation)

add_library(football_pitch_ball INTERFACE)
add_library(football::pitch_ball ALIAS football_pitch_ball)
target_sources(football_pitch_ball INTERFACE FILE_SET HEADERS
  BASE_DIRS ${PROJECT_SOURCE_DIR}/include
  FILES ${PROJECT_SOURCE_DIR}/include/football/pitch/ball_world.hpp)
target_compile_features(football_pitch_ball INTERFACE cxx_std_23)
target_link_libraries(football_pitch_ball INTERFACE football::pitch football::ball)

add_subdirectory(src/sim)
add_subdirectory(src/ai)

# The core shared library. Everything under src/ is compiled into libsimcore.so;
# executables (tools/) link it and never get compiled into it.
add_library(simcore SHARED src/env.cpp)
add_library(football::simcore ALIAS simcore)
target_sources(simcore PUBLIC FILE_SET HEADERS
  BASE_DIRS ${PROJECT_SOURCE_DIR}/include
  FILES ${PROJECT_SOURCE_DIR}/include/football/env.hpp)
target_link_libraries(simcore
  PUBLIC football::sim_contracts football::ai_contracts football::ball
  PRIVATE football::ai BqLog
    # Keep the existing runtime exports, including unreferenced GetRoleFromString.
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,football_sim_domain>")
football_configure_target(simcore)

# Executable-side args/fixtures live under tools/app. app_support/recording stay
# EXCLUDE_FROM_ALL and are pulled in only by the football_sim executable.
add_subdirectory(tools/app)

if(BUILD_TESTING)
  CPMAddPackage(NAME Catch2 GITHUB_REPOSITORY catchorg/Catch2 GIT_TAG v3.7.1
    OPTIONS "CATCH_BUILD_TESTING OFF")
  add_subdirectory(tools/animBaker)
  add_subdirectory(unittests)
endif()
