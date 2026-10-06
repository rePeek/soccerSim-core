cmake_minimum_required(VERSION 3.24)
project(gameplayfootball LANGUAGES CXX)
set(OUTPUT_LIB_NAME game)
set(PACKAGE gameplayfootball)
include(CTest)
option(FOOTBALL_BUILD_APP "Build the CLI executable and its sample inputs" ON)

# Shared build policy only; each module owns its sources, exports and link edges.
function(football_configure_target tgt)
  target_include_directories(${tgt} PUBLIC ${PROJECT_SOURCE_DIR}/src)
  target_compile_features(${tgt} PUBLIC cxx_std_23)
  target_compile_definitions(${tgt} PUBLIC
    GFOOTBALL_BAKED_ANIM_PATH="${PROJECT_SOURCE_DIR}/assets/runtime/animations.simanim")
  set_target_properties(${tgt} PROPERTIES POSITION_INDEPENDENT_CODE ON)
  get_target_property(_type ${tgt} TYPE)
  if(_type STREQUAL "EXECUTABLE")
    # Preserve existing CLI/tool locations while their recipes move to modules.
    set_target_properties(${tgt} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
  endif()
endfunction()

add_subdirectory(src/foundation)
add_subdirectory(src/model)
add_subdirectory(src/support)
add_subdirectory(src/sim)
add_subdirectory(src/ai)
add_subdirectory(src/env)

add_library(${OUTPUT_LIB_NAME} SHARED $<TARGET_OBJECTS:football_engine>)
add_library(football::game ALIAS ${OUTPUT_LIB_NAME})
target_link_libraries(${OUTPUT_LIB_NAME}
  PUBLIC football::sim_contracts football::ai
  # Package all runtime units, preserving unreferenced public symbols (notably
  # GetRoleFromString) when their build ownership moves out of the env object.
  PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,football_sim>")
football_configure_target(${OUTPUT_LIB_NAME})

# Input/fixture archives are EXCLUDE_FROM_ALL, but remain available to tests and
# explicit consumers even when CLI and testing are both disabled.
add_subdirectory(src/app)
include(cmake/module_dependencies.cmake)

if(BUILD_TESTING)
  set(CPM_SOURCE_CACHE "${PROJECT_SOURCE_DIR}/.cache/CPM"
    CACHE PATH "CPM dependency source cache")
  include(cmake/CPM.cmake)
  CPMAddPackage(NAME Catch2 GITHUB_REPOSITORY catchorg/Catch2 GIT_TAG v3.7.1
    OPTIONS "CATCH_BUILD_TESTING OFF")
  list(APPEND CMAKE_MODULE_PATH "${Catch2_SOURCE_DIR}/extras")
  include(Catch)
  add_subdirectory(tools/animBaker)
  add_subdirectory(tools)
  add_subdirectory(test)
endif()
