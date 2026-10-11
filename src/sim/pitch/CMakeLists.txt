cmake_minimum_required(VERSION 3.28)

# football.pitch — C++20 named module. It owns the pitch -> BallWorld assembly
# (ground plane, posts, crossbars, ground material, net geometry). It depends
# on football::ball (BallWorld/collider values) and football::model (Pitch).
# football::ball must never depend on football::pitch.

add_library(football_pitch STATIC)
add_library(football::pitch ALIAS football_pitch)

target_sources(football_pitch
  PUBLIC
    FILE_SET CXX_MODULES
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/src
    FILES
      src/pitch.cppm
  PRIVATE
    src/pitch.cpp
)

target_compile_features(football_pitch PUBLIC cxx_std_23)

target_link_libraries(football_pitch
  PUBLIC
    football::ball
    football::model
)

football_configure_target(football_pitch)
