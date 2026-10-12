cmake_minimum_required(VERSION 3.24)

# Independent football::ball module.
#
# Public contract:  ${PROJECT_SOURCE_DIR}/include/football/ball/*
# Private impl:     *.cpp + private headers in this directory
#
# football::ball must never depend on football_sim_domain, football_player,
# football_referee, football_event, football_observation, football::pitch or
# football::pitch. It consumes foundation math plus its own BallWorld.

add_library(football_ball STATIC)
add_library(football::ball ALIAS football_ball)

target_sources(football_ball
  PRIVATE
    ball.cpp
    ball_netting.cpp
    ball_collision.cpp
    ball_deforming_capsule.cpp
    ball_response.cpp
)

target_sources(football_ball
  PUBLIC
    FILE_SET HEADERS
    BASE_DIRS ${PROJECT_SOURCE_DIR}/include
    FILES
      ${PROJECT_SOURCE_DIR}/include/football/ball/ball.hpp
      ${PROJECT_SOURCE_DIR}/include/football/ball/ball_model.hpp
      ${PROJECT_SOURCE_DIR}/include/football/ball/ball_world.hpp
      ${PROJECT_SOURCE_DIR}/include/football/ball/ball_state.hpp
      ${PROJECT_SOURCE_DIR}/include/football/ball/ball_impulse.hpp
      ${PROJECT_SOURCE_DIR}/include/football/ball/ball_environment.hpp
      ${PROJECT_SOURCE_DIR}/include/football/ball/ball_timing.hpp
      ${PROJECT_SOURCE_DIR}/include/football/ball/collider.hpp
      ${PROJECT_SOURCE_DIR}/include/football/ball/ball_contact.hpp
      ${PROJECT_SOURCE_DIR}/include/football/ball/ball_response.hpp
)

target_include_directories(football_ball
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(football_ball PUBLIC cxx_std_23)

target_link_libraries(football_ball
  PUBLIC
    football::foundation
)

football_configure_target(football_ball)
