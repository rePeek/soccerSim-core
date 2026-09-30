cmake_minimum_required(VERSION 3.24)

# This module is intentionally usable without env, sim, data, or animation.
# Configure it directly to validate that its only project dependency remains
# foundation.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(football_controller LANGUAGES CXX)
endif()

if(NOT TARGET football_foundation)
  add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../foundation
                   ${CMAKE_CURRENT_BINARY_DIR}/foundation)
endif()

add_library(football_controller STATIC
  external_controller.hpp
  grf/action.hpp
  grf/grf_action_controller.cpp
  grf/grf_action_controller.hpp
)

add_library(football::controller ALIAS football_controller)
target_include_directories(football_controller PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>)
target_compile_features(football_controller PUBLIC cxx_std_23)
target_link_libraries(football_controller PUBLIC football_foundation)
set_target_properties(football_controller PROPERTIES
  POSITION_INDEPENDENT_CODE ON)
