cmake_minimum_required(VERSION 3.24)
project(gameplayfootball LANGUAGES CXX)
include(CTest)
option(FOOTBALL_BUILD_APP "Build the CLI executable and its sample inputs" ON)

# Shared build policy only; each module owns its sources, exports and link edges.
function(football_configure_target tgt)
  target_include_directories(${tgt} PUBLIC ${PROJECT_SOURCE_DIR}/src)
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
add_subdirectory(src/model)
add_subdirectory(src/sim)
add_subdirectory(src/ai)

add_library(game SHARED src/env.cpp)
add_library(football::game ALIAS game)
target_sources(game PUBLIC FILE_SET HEADERS
  BASE_DIRS ${PROJECT_SOURCE_DIR}/src
  FILES src/env.hpp)
target_link_libraries(game
  PUBLIC football::model football::sim_contracts football::ai_contracts football::ball
  PRIVATE football::ai BqLog
    # Keep the existing runtime exports, including unreferenced GetRoleFromString.
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,football_sim>")
football_configure_target(game)

# Executable-side args/fixtures stay EXCLUDE_FROM_ALL but are available to tests
# and explicit consumers even when CLI and testing are both disabled.
add_subdirectory(src/app)

if(BUILD_TESTING)
  CPMAddPackage(NAME Catch2 GITHUB_REPOSITORY catchorg/Catch2 GIT_TAG v3.7.1
    OPTIONS "CATCH_BUILD_TESTING OFF")
  add_subdirectory(tools/animBaker)
  add_subdirectory(tools)
  add_subdirectory(test)
endif()
