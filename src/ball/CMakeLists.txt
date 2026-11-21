cmake_minimum_required(VERSION 3.24)

# BallState is the state contract: header-only so recorders such as
# football::history can depend on it without the physics library.
add_library(football_ball_state INTERFACE)
add_library(football::ball_state ALIAS football_ball_state)
target_sources(football_ball_state INTERFACE FILE_SET HEADERS
  BASE_DIRS ${PROJECT_SOURCE_DIR}/include
  FILES
    ${PROJECT_SOURCE_DIR}/include/football/ball/ball_state.hpp)
target_compile_features(football_ball_state INTERFACE cxx_std_23)
target_link_libraries(football_ball_state INTERFACE football::foundation)

add_library(football_ball STATIC)
add_library(football::ball ALIAS football_ball)

target_sources(football_ball
  PRIVATE
    ball.cpp
)

target_sources(football_ball
  PUBLIC
    FILE_SET HEADERS
    BASE_DIRS ${PROJECT_SOURCE_DIR}/include
    FILES
      ${PROJECT_SOURCE_DIR}/include/football/ball/ball.hpp
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
    football::models football::foundation football::geometry football::pitch
    football::ball_state
  PRIVATE
    football::collision football::log)

football_configure_target(football_ball)
