cmake_minimum_required(VERSION 3.24)

# football.pitch — header-only pitch -> BallWorld assembly. It depends on
# football::ball (BallWorld/collider values) and football::model (Pitch).
# football::ball must never depend on football::pitch. No module toolchain.
add_library(football_pitch INTERFACE)
add_library(football::pitch ALIAS football_pitch)
target_sources(football_pitch INTERFACE FILE_SET HEADERS
  BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include
  FILES include/football/pitch/pitch.hpp)
target_compile_features(football_pitch INTERFACE cxx_std_23)
target_link_libraries(football_pitch INTERFACE football::ball football::model)
