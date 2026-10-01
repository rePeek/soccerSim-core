cmake_minimum_required(VERSION 3.24)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(football_state LANGUAGES CXX)
endif()

if(NOT TARGET football_foundation)
  add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../foundation
                   ${CMAKE_CURRENT_BINARY_DIR}/foundation)
endif()

if(NOT TARGET football_domain)
  add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../domain
                   ${CMAKE_CURRENT_BINARY_DIR}/domain)
endif()

add_library(football_state INTERFACE)
add_library(football::state ALIAS football_state)
target_include_directories(football_state INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>)
target_compile_features(football_state INTERFACE cxx_std_23)
target_link_libraries(football_state INTERFACE
  football_foundation football_domain)
