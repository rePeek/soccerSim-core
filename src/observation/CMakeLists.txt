cmake_minimum_required(VERSION 3.24)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(football_observation LANGUAGES CXX)
endif()

if(NOT TARGET football_foundation)
  add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../foundation
                   ${CMAKE_CURRENT_BINARY_DIR}/foundation)
endif()

if(NOT TARGET football_model)
  add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../model
                   ${CMAKE_CURRENT_BINARY_DIR}/model)
endif()

# Passive observation contract: the value types a caller can read back from the
# environment. It owns no mutable match state and no observation logic.
add_library(football_observation INTERFACE)
add_library(football::observation ALIAS football_observation)
target_include_directories(football_observation INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>)
target_compile_features(football_observation INTERFACE cxx_std_23)
target_link_libraries(football_observation INTERFACE
  football_foundation football_model)
