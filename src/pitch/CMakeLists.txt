# football::pitch owns the pitch description and the static physics scene
# (ground/posts/crossbars as geometry colliders). ball and player read it;
# pitch never depends on them. pitch_frame.hpp stays header-only: it is pure,
# stateless geometry over foundation math.
add_library(football_pitch STATIC pitch.cpp)
add_library(football::pitch ALIAS football_pitch)
target_sources(football_pitch PUBLIC FILE_SET HEADERS
  BASE_DIRS ${PROJECT_SOURCE_DIR}/include
  FILES
    ${PROJECT_SOURCE_DIR}/include/football/pitch/pitch.hpp
    ${PROJECT_SOURCE_DIR}/include/football/pitch/pitch_frame.hpp)
target_link_libraries(football_pitch
  PUBLIC football::foundation football::geometry)
football_configure_target(football_pitch)
