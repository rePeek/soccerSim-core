cmake_minimum_required(VERSION 3.24)

set(OUTPUT_LIB_NAME game)
project(gameplayfootball LANGUAGES CXX)
include(CTest)  # defines BUILD_TESTING
option(FOOTBALL_BUILD_APP "Build the CLI executable and its sample inputs" ON)

set(PACKAGE gameplayfootball)

include(sources.cmake)

function(football_configure_target tgt)
  target_include_directories(${tgt} PUBLIC
    ${PROJECT_SOURCE_DIR}/src)
  target_compile_features(${tgt} PUBLIC cxx_std_23)
  target_compile_definitions(${tgt} PUBLIC
    GFOOTBALL_BAKED_ANIM_PATH="${PROJECT_SOURCE_DIR}/assets/runtime/animations.simanim")
endfunction()

# Foundation owns its source list and can also be consumed independently with
# add_subdirectory(path/to/src/foundation).
add_subdirectory(src/foundation)
add_subdirectory(src/model)
add_subdirectory(src/observation)
add_subdirectory(src/control)
add_subdirectory(src/support)
add_subdirectory(src/controller)

# --- object libraries: one translation unit at a time -----------------------
# The simulation's runtime animation subsystem (src/sim/animation). It depends
# on foundation only, and is kept as its own archive so the offline baker can
# load and verify a baked .simanim without linking the simulation.
add_library(football_animation_objects OBJECT
  ${ANIMATION_SOURCES} ${ANIMATION_HEADERS})
target_link_libraries(football_animation_objects PRIVATE football_foundation)

# Offline baker sources. Built for tools and tests, never linked into the
# runtime shared library.
if(BUILD_TESTING)
add_library(football_legacy_anim OBJECT ${LEGACY_ANIM_SOURCES})
target_link_libraries(football_legacy_anim PUBLIC football_support football_foundation)
target_include_directories(football_legacy_anim PUBLIC
  ${PROJECT_SOURCE_DIR}/tools/animBaker)
endif()

# --- static libraries -------------------------------------------------------
# The layering below spells out the source DAG in build order:
#
#   foundation <- sim/animation <- sim <- engine <- game
#         ^                            ^
#         ├── model/control ────────────┘
#         └── controller (temporary legacy input)
#
#   observation is a read-only contract leaf above model/foundation.
#   app fixtures/importers -> model/support (executable-only; no core edge)
#
# The runtime animation archive below depends directly on foundation only;
# source importers use support for XML, files, text, and configuration. Control
# owns the new decision domain. Controller remains only while the legacy input
# path is removed; it is not a future simulation boundary.
# Declaring the edges here keeps archive order correct instead of relying on
# a hand-maintained link line.

add_library(football_animation STATIC
  $<TARGET_OBJECTS:football_animation_objects>)
target_link_libraries(football_animation PUBLIC football_foundation)

add_library(football_app_support STATIC EXCLUDE_FROM_ALL
  ${APP_SUPPORT_SOURCES} ${APP_SUPPORT_HEADERS})
target_link_libraries(football_app_support PUBLIC football_support football_model)
football_configure_target(football_app_support)

add_library(football_sim STATIC ${GAME_SOURCES} ${GAME_HEADERS})
target_link_libraries(football_sim PUBLIC football_animation football_support
  football_model football_observation football_control football_controller)

# The decision implementations. ai -> sim only: football_sim must never link
# football_ai, or the domain dependency would point the wrong way.
add_library(football_ai STATIC ${AI_SOURCES} ${AI_HEADERS})
target_link_libraries(football_ai PUBLIC football_sim)
football_configure_target(football_ai)

# env is the composition root: game_env.cpp names the default ai factories and
# the engine object layer therefore links football_ai. Keep the dependency in
# that single translation unit; game_env.hpp exposes no AI type.
add_library(football_engine OBJECT
  ${CORE_SOURCES} ${CORE_HEADERS}
  src/env/game_env.cpp src/env/game_env.hpp)
target_link_libraries(football_engine PRIVATE football_controller football_model
  football_sim football_observation football_support football_control football_ai)

add_library(${OUTPUT_LIB_NAME} SHARED $<TARGET_OBJECTS:football_engine>)
target_link_libraries(${OUTPUT_LIB_NAME}
  PUBLIC football_observation football_control
  PRIVATE football_sim football_controller football_ai)

if(FOOTBALL_BUILD_APP)
  add_executable(football_app ${APP_MAIN_SOURCES})
  target_link_libraries(football_app PRIVATE ${OUTPUT_LIB_NAME} football_app_support)
  football_configure_target(football_app)
endif()

foreach(tgt IN ITEMS
    football_animation_objects
    football_animation football_sim football_ai
    football_engine ${OUTPUT_LIB_NAME})
  football_configure_target(${tgt})
endforeach()
if(BUILD_TESTING)
  football_configure_target(football_legacy_anim)
endif()

# --- tests ------------------------------------------------------------------
if(BUILD_TESTING)
  # Catch2 is fetched through CPM; its checkout is cached beside clangd's in
  # .cache/, so reconfiguring never re-downloads it.
  set(CPM_SOURCE_CACHE "${PROJECT_SOURCE_DIR}/.cache/CPM"
      CACHE PATH "CPM dependency source cache")
  include(${PROJECT_SOURCE_DIR}/cmake/CPM.cmake)
  CPMAddPackage(
    NAME Catch2
    GITHUB_REPOSITORY catchorg/Catch2
    GIT_TAG v3.7.1
    OPTIONS "CATCH_BUILD_TESTING OFF")
  list(APPEND CMAKE_MODULE_PATH "${Catch2_SOURCE_DIR}/extras")
  include(Catch)

  add_subdirectory(test)
add_executable(football_smoke tools/football_smoke.cpp)
target_link_libraries(football_smoke PRIVATE ${OUTPUT_LIB_NAME} football_app_support)
football_configure_target(football_smoke)

add_executable(football_game_env_test tools/game_env_test.cpp)
target_link_libraries(football_game_env_test PRIVATE ${OUTPUT_LIB_NAME} football_app_support)
football_configure_target(football_game_env_test)
add_test(NAME football_game_env_test COMMAND football_game_env_test)

add_executable(football_player_identity_test tools/player_identity_test.cpp)
# Runs Simulation directly: numbering no longer needs environment bindings.
target_link_libraries(football_player_identity_test PRIVATE ${OUTPUT_LIB_NAME} football_app_support)
football_configure_target(football_player_identity_test)
add_test(NAME football_player_identity_test COMMAND football_player_identity_test)

add_executable(football_control_test tools/player_control_set_test.cpp)
target_link_libraries(football_control_test PRIVATE football_control)
football_configure_target(football_control_test)
add_test(NAME football_control_test COMMAND football_control_test)

add_executable(football_model_test tools/model_test.cpp)
target_link_libraries(football_model_test PRIVATE football::model)
add_test(NAME football_model_test COMMAND football_model_test)

add_test(NAME model_boundary_guard
  COMMAND sh ${CMAKE_SOURCE_DIR}/tools/model_boundary_guard.sh
    ${CMAKE_SOURCE_DIR}/src)

add_executable(football_anim_baker tools/animBaker/anim_baker_main.cpp)
target_link_libraries(football_anim_baker PRIVATE
  football_legacy_anim football_animation football_foundation football_support)
football_configure_target(football_anim_baker)

add_test(NAME football_anim_baker_standalone_guard
  COMMAND ${CMAKE_SOURCE_DIR}/tools/animBaker/anim_baker_standalone_guard.sh
    ${CMAKE_SOURCE_DIR}/tools/animBaker/anim_baker_main.cpp)

add_test(NAME football_anim_bake_determinism
  COMMAND ${CMAKE_SOURCE_DIR}/tools/animBaker/anim_bake_determinism.sh
    $<TARGET_FILE:football_anim_baker> ${CMAKE_SOURCE_DIR}/data)

  add_executable(football_regression tools/football_regression.cpp)
  target_link_libraries(football_regression PRIVATE
    ${OUTPUT_LIB_NAME} football_app_support football_legacy_anim)
  football_configure_target(football_regression)

  add_test(NAME football_regression COMMAND football_regression)
  set_tests_properties(football_regression PROPERTIES
    ENVIRONMENT "GFOOTBALL_DATA_DIR=${CMAKE_SOURCE_DIR}/data")
  add_test(NAME football_animation_ab COMMAND football_regression --animation-ab)
  set_tests_properties(football_animation_ab PROPERTIES
    ENVIRONMENT "GFOOTBALL_DATA_DIR=${CMAKE_SOURCE_DIR}/data")
  add_test(NAME football_animation_ab_lifecycle
    COMMAND football_regression --animation-ab-lifecycle)
  set_tests_properties(football_animation_ab_lifecycle PROPERTIES
    ENVIRONMENT "GFOOTBALL_DATA_DIR=${CMAKE_SOURCE_DIR}/data")
# Declared link edges cannot be enforced by the compiler: every target builds
# with -I src because headers cross-reference module/... paths, so an
# undeclared include still compiles. Export the real transitive closure so
# module_dependency_guard can check each module's includes against the edges
# that were actually declared.
function(football_direct_links tgt out_var)
  set(_links)
  foreach(_prop LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
    get_target_property(_values ${tgt} ${_prop})
    if(_values)
      foreach(_value IN LISTS _values)
        if(TARGET ${_value})
          list(APPEND _links ${_value})
        endif()
      endforeach()
    endif()
  endforeach()
  set(${out_var} ${_links} PARENT_SCOPE)
endfunction()

function(football_link_closure tgt out_var)
  set(_closure ${tgt})
  set(_queue ${tgt})
  while(_queue)
    list(POP_FRONT _queue _current)
    football_direct_links(${_current} _links)
    foreach(_link IN LISTS _links)
      if(NOT _link IN_LIST _closure)
        list(APPEND _closure ${_link})
        list(APPEND _queue ${_link})
      endif()
    endforeach()
  endwhile()
  set(${out_var} ${_closure} PARENT_SCOPE)
endfunction()

set(_module_dependency_report "")
foreach(_module_target IN ITEMS
    football_foundation football_model football_observation football_control
    football_controller football_support football_animation football_sim
    football_ai football_engine football_app_support)
  football_link_closure(${_module_target} _closure)
  string(REPLACE ";" " " _closure_text "${_closure}")
  string(APPEND _module_dependency_report
    "${_module_target}: ${_closure_text}\n")
endforeach()
file(WRITE "${CMAKE_BINARY_DIR}/module_dependencies.txt"
     "${_module_dependency_report}")

add_test(NAME module_dependency_guard
  COMMAND sh ${CMAKE_SOURCE_DIR}/tools/module_dependency_guard.sh
    ${CMAKE_SOURCE_DIR}/src ${CMAKE_BINARY_DIR}/module_dependencies.txt)


# Keep the completed headless boundary from growing back through a convenient
# include or an accidental link dependency (graphics and Boost are both
# rejected by this guard).
add_test(NAME football_headless_core_guard
  COMMAND ${CMAKE_SOURCE_DIR}/tools/football_headless_core_guard.sh
    $<TARGET_FILE:${OUTPUT_LIB_NAME}> ${CMAKE_SOURCE_DIR}/src)

add_test(NAME runtime_animation_boundary_guard
  COMMAND ${CMAKE_SOURCE_DIR}/tools/runtime_animation_boundary_guard.sh
    ${CMAKE_SOURCE_DIR}/src)

add_test(NAME foundation_boundary_guard
  COMMAND ${CMAKE_SOURCE_DIR}/tools/foundation_boundary_guard.sh
    ${CMAKE_SOURCE_DIR}/src)

add_test(NAME legacy_validation_guard
  COMMAND ${CMAKE_SOURCE_DIR}/tools/legacy_validation_guard.sh
    ${CMAKE_SOURCE_DIR})

add_test(NAME controller_boundary_guard
  COMMAND ${CMAKE_SOURCE_DIR}/tools/controller_boundary_guard.sh
    ${CMAKE_SOURCE_DIR}/src)
endif()  # BUILD_TESTING
