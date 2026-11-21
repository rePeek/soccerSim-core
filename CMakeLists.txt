cmake_minimum_required(VERSION 3.24)
project(gameplayfootball LANGUAGES C CXX)
include(CTest)

# Optional local build acceleration: use ccache whenever the dev shell
# provides it. This is a silent no-op when ccache is absent.
find_program(FOOTBALL_CCACHE_PROGRAM ccache)
if(FOOTBALL_CCACHE_PROGRAM)
  set(CMAKE_CXX_COMPILER_LAUNCHER "${FOOTBALL_CCACHE_PROGRAM}"
      CACHE STRING "C++ compiler launcher (ccache when available)")
  set(CMAKE_C_COMPILER_LAUNCHER "${FOOTBALL_CCACHE_PROGRAM}"
      CACHE STRING "C compiler launcher (ccache when available)")
endif()

function(football_configure_target tgt)
  target_compile_features(${tgt} PUBLIC cxx_std_23)
  set_target_properties(${tgt} PROPERTIES POSITION_INDEPENDENT_CODE ON)
  get_target_property(_type ${tgt} TYPE)
  if(_type STREQUAL "EXECUTABLE")
    set_target_properties(${tgt} PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
  endif()
endfunction()

set(CPM_SOURCE_CACHE "${PROJECT_SOURCE_DIR}/.cache/CPM"
  CACHE PATH "CPM dependency source cache")
include(cmake/CPM.cmake)
include(cmake/bqlog.cmake)

# Single-source C model values, usable without any simulation implementation.
add_library(football_models INTERFACE)
add_library(football::models ALIAS football_models)
target_sources(football_models INTERFACE FILE_SET HEADERS
  BASE_DIRS ${PROJECT_SOURCE_DIR}/include
  FILES
    ${PROJECT_SOURCE_DIR}/include/football/models.h)

add_subdirectory(src/log)
add_subdirectory(src/foundation)
add_subdirectory(src/pitch)
add_subdirectory(src/ball)
add_subdirectory(src/history)

# One match: Ball physics on a Pitch, no team, player or rules layer. Env is
# the synchronous match; the runner plays it on a worker, so the C ABI only
# creates and releases sessions. History records frames in-process and is not
# delivered over the ABI yet.
add_library(simcore SHARED src/env.cpp src/runner.cpp src/capi.cpp)
add_library(football::simcore ALIAS simcore)
# The C ABI is the only supported contract. env.hpp and runner.hpp stay plain
# headers on the include path but are not exported as public headers.
target_sources(simcore PUBLIC FILE_SET HEADERS
  BASE_DIRS ${PROJECT_SOURCE_DIR}/include
  FILES
    ${PROJECT_SOURCE_DIR}/include/football/capi.h)
target_compile_definitions(simcore PRIVATE FOOTBALL_CAPI_BUILD)
target_include_directories(simcore PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(simcore
  PUBLIC football::ball football::pitch
  PRIVATE football::log football::history)
football_configure_target(simcore)

if(BUILD_TESTING)
  include(cmake/catch2.cmake)
  add_subdirectory(unittests)
endif()
