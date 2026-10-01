cmake_minimum_required(VERSION 3.24)

# Shared football-domain value identities. This layer is independent of state,
# control, AI, simulation, and infrastructure.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(football_domain LANGUAGES CXX)
endif()

add_library(football_domain INTERFACE)
add_library(football::domain ALIAS football_domain)
target_include_directories(football_domain INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>)
target_compile_features(football_domain INTERFACE cxx_std_23)
