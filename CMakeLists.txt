cmake_minimum_required(VERSION 3.24)

set(OUTPUT_LIB_NAME game)
project(gameplayfootball LANGUAGES CXX)
include(CTest)  # defines BUILD_TESTING

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
add_subdirectory(src/control)
add_subdirectory(src/support)
add_subdirectory(src/controller)

# --- object libraries: one translation unit at a time -----------------------
# Runtime animation. Depends on the bottom layer only.
add_library(football_animation_objects OBJECT
  ${ANIMATION_SOURCES} ${ANIMATION_HEADERS})
target_link_libraries(football_animation_objects PRIVATE football_foundation)

# Offline baker sources. Built for tools and tests, never linked into the
# runtime shared library.
add_library(football_legacy_anim OBJECT ${LEGACY_ANIM_SOURCES})
target_link_libraries(football_legacy_anim PUBLIC football_support football_foundation)
target_include_directories(football_legacy_anim PUBLIC
  ${PROJECT_SOURCE_DIR}/tools/animBaker)

# --- static libraries -------------------------------------------------------
# The layering below spells out the source DAG in build order:
#
#   foundation <- support <- data <- sim <- engine
#         ^                  ^
#         ├── control ───────┘
#         └── controller (temporary legacy input)
#
# Runtime animation depends directly on foundation only; source importers
# use support for XML, files, text, and configuration. Control owns the new
# decision domain. Controller remains only while the legacy input path is
# removed; it is not a future simulation boundary.
# Declaring the edges here keeps archive order correct instead of relying on
# a hand-maintained link line.

add_library(football_animation STATIC
  $<TARGET_OBJECTS:football_animation_objects>)
target_link_libraries(football_animation PUBLIC football_foundation)

add_library(football_data STATIC ${DATA_SOURCES} ${DATA_HEADERS})
target_link_libraries(football_data PUBLIC football_animation football_support)

add_library(football_sim STATIC ${GAME_SOURCES} ${GAME_HEADERS})
target_link_libraries(football_sim PUBLIC football_data football_control football_controller)

# --- engine object layer + the shared library -------------------------------
add_library(football_engine OBJECT
  ${CORE_SOURCES} ${CORE_HEADERS}
  src/env/game_env.cpp src/env/game_env.hpp
  src/ai/ai_keyboard.cpp src/ai/ai_keyboard.hpp)
target_link_libraries(football_engine PRIVATE football_controller)

add_library(${OUTPUT_LIB_NAME} SHARED $<TARGET_OBJECTS:football_engine>)
target_link_libraries(${OUTPUT_LIB_NAME} PRIVATE football_sim football_controller)

foreach(tgt IN ITEMS
    football_animation_objects football_legacy_anim
    football_animation football_data football_sim
    football_engine ${OUTPUT_LIB_NAME})
  football_configure_target(${tgt})
endforeach()

# --- tests ------------------------------------------------------------------
add_executable(football_smoke tools/football_smoke.cpp)
target_link_libraries(football_smoke PRIVATE ${OUTPUT_LIB_NAME})
football_configure_target(football_smoke)

add_executable(football_control_test tools/control_system_test.cpp)
target_link_libraries(football_control_test PRIVATE football_control)
football_configure_target(football_control_test)
add_test(NAME football_control_test COMMAND football_control_test)

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

if(BUILD_TESTING)
  add_executable(football_regression tools/football_regression.cpp)
  target_link_libraries(football_regression PRIVATE ${OUTPUT_LIB_NAME} football_legacy_anim)
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
endif()

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
