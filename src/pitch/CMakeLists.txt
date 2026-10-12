cmake_minimum_required(VERSION 3.24)

# football.pitch — the pitch domain.
#
# football::pitch owns the read-only Pitch description and depends only on
# foundation. football::pitch_ball owns the header-only Pitch -> BallWorld
# assembly and is the only target that pulls football::ball together with
# Pitch. football::ball must never depend on either. No module toolchain.

add_library(football_pitch INTERFACE)
add_library(football::pitch ALIAS football_pitch)
target_sources(football_pitch INTERFACE FILE_SET HEADERS
  BASE_DIRS ${PROJECT_SOURCE_DIR}/include
  FILES ${PROJECT_SOURCE_DIR}/include/football/pitch/pitch.hpp)
target_compile_features(football_pitch INTERFACE cxx_std_23)
target_link_libraries(football_pitch INTERFACE football::foundation)

add_library(football_pitch_ball INTERFACE)
add_library(football::pitch_ball ALIAS football_pitch_ball)
target_sources(football_pitch_ball INTERFACE FILE_SET HEADERS
  BASE_DIRS ${PROJECT_SOURCE_DIR}/include
  FILES ${PROJECT_SOURCE_DIR}/include/football/pitch/ball_world.hpp)
target_compile_features(football_pitch_ball INTERFACE cxx_std_23)
target_link_libraries(football_pitch_ball INTERFACE football::pitch football::ball)
