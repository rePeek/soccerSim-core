cmake_minimum_required(VERSION 3.24)

# The control domain is independent of env, sim, input protocols, and
# animation. It owns football decisions, not device state.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(football_control LANGUAGES CXX)
endif()

if(NOT TARGET football_foundation)
  add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../foundation
                   ${CMAKE_CURRENT_BINARY_DIR}/foundation)
endif()

add_library(football_control STATIC
  control_ids.hpp
  world_state_view.hpp
  control_system.cpp
  control_system.hpp
  coach/coach_control.hpp
  coach/coach_control_context.hpp
  coach/team_plan.hpp
  player/player_control.hpp
  player/player_control_context.hpp
  player/player_intent.hpp)

add_library(football::control ALIAS football_control)
target_include_directories(football_control PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>)
target_compile_features(football_control PUBLIC cxx_std_23)
target_link_libraries(football_control PUBLIC football_foundation)
set_target_properties(football_control PROPERTIES POSITION_INDEPENDENT_CODE ON)
