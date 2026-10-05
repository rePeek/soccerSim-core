cmake_minimum_required(VERSION 3.24)

# Static football-domain descriptions and identities. STL and other model
# headers only; no runtime, foundation, infrastructure or file-format dependency.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(football_model LANGUAGES CXX)
endif()

add_library(football_model INTERFACE)
add_library(football::model ALIAS football_model)
target_include_directories(football_model INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>)
target_compile_features(football_model INTERFACE cxx_std_23)
