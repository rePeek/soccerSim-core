cmake_minimum_required(VERSION 3.24)

# Independent football::ball module.
#
# Public contract:  include/football/ball/*
# Private impl:     src/*
#
# football::ball must never depend on football_sim, football_player,
# football_referee, football_event, football_observation, football::model or
# football::pitch. It consumes foundation math plus its own BallWorld.

add_library(football_ball STATIC)
add_library(football::ball ALIAS football_ball)

target_sources(football_ball
  PRIVATE
    src/ball.cpp
    src/ball_contact.cpp
    src/ball_collision.cpp
    src/ball_deforming_capsule.cpp
    src/ball_response.cpp
)

target_sources(football_ball
  PUBLIC
    FILE_SET HEADERS
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include
    FILES
      include/football/ball/ball.hpp
      include/football/ball/ball_model.hpp
      include/football/ball/ball_world.hpp
      include/football/ball/ball_state.hpp
      include/football/ball/ball_impulse.hpp
      include/football/ball/ball_environment.hpp
      include/football/ball/ball_timing.hpp
      include/football/ball/collider.hpp
      include/football/ball/ball_contact.hpp
      include/football/ball/ball_response.hpp
)

target_include_directories(football_ball
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_features(football_ball PUBLIC cxx_std_23)

target_link_libraries(football_ball
  PUBLIC
    football::foundation
)

football_configure_target(football_ball)
