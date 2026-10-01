cmake_minimum_required(VERSION 3.24)

# Passive football control contracts. This module owns no decision algorithms,
# input protocols, mutable simulation state, or animation state.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(football_control LANGUAGES CXX)
endif()

if(NOT TARGET football_state)
  add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../state
                   ${CMAKE_CURRENT_BINARY_DIR}/state)
endif()

add_library(football_control INTERFACE)
add_library(football::control ALIAS football_control)
target_include_directories(football_control INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>)
target_compile_features(football_control INTERFACE cxx_std_23)
target_link_libraries(football_control INTERFACE football_state)
