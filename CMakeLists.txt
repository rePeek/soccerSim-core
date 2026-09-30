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
endfunction()

# --- object libraries: one translation unit at a time -----------------------
add_library(football_base OBJECT
  ${BASE_SOURCES} ${BASE_HEADERS} ${BASE_GEOMETRY_HEADERS} ${BASE_MATH_HEADERS})
add_library(football_types OBJECT ${TYPES_SOURCES} ${TYPES_HEADERS})
add_library(football_utils OBJECT
  ${UTILS_SOURCES} ${UTILS_HEADERS} ${UTILS_EXT_HEADERS})

# --- static libraries -------------------------------------------------------
# football_foundation bundles base/types/utils behind one archive boundary,
# mirroring the historical blunted2 aggregation without changing which
# translation units land in the final shared library.
add_library(football_foundation STATIC
  ${BLUNTED_CORE_SOURCES} ${BLUNTED_CORE_HEADERS}
  $<TARGET_OBJECTS:football_base>
  $<TARGET_OBJECTS:football_types>
  $<TARGET_OBJECTS:football_utils>)
add_library(football_sim STATIC ${GAME_SOURCES} ${GAME_HEADERS})
add_library(football_data STATIC ${DATA_SOURCES} ${DATA_HEADERS})

# --- engine object layer + the shared library -------------------------------
add_library(football_engine OBJECT
  ${CORE_SOURCES} ${CORE_HEADERS}
  src/env/game_env.cpp src/env/game_env.hpp
  src/ai/ai_keyboard.cpp src/ai/ai_keyboard.hpp)

add_library(${OUTPUT_LIB_NAME} SHARED $<TARGET_OBJECTS:football_engine>)
# Order matters: static archives are scanned once, left to right, so the
# leaf libraries that provide symbols (football_foundation) must come after
# the libraries that consume them (football_sim, football_data).
target_link_libraries(${OUTPUT_LIB_NAME} PRIVATE
  football_sim football_data football_foundation)

foreach(tgt IN ITEMS
    football_base football_types football_utils
    football_foundation football_sim football_data
    football_engine ${OUTPUT_LIB_NAME})
  football_configure_target(${tgt})
endforeach()

# --- tests ------------------------------------------------------------------
add_executable(football_smoke tools/football_smoke.cpp)
target_link_libraries(football_smoke PRIVATE ${OUTPUT_LIB_NAME})
football_configure_target(football_smoke)

if(BUILD_TESTING)
  add_executable(football_regression tools/football_regression.cpp)
  target_link_libraries(football_regression PRIVATE ${OUTPUT_LIB_NAME})
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
