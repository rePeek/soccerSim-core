# ANIM-2 A12: the Animation module owns two archives.
#
# football::animation_base — the .simanim format, the baked data structures, the
# artifact reader and the base/coarse candidate selection. The offline baker
# links ONLY this archive, so it never depends on the runtime algorithms.
#
# football::animation — the runtime algorithms on top of the base: full match
# refinement, touch geometry/scoring, pose sampling/blending, playback and
# transition state. Player links this.
#
# Both are declared here (not in the root CMakeLists) so the module owns its
# targets, sources and header sets.

add_library(football_animation_base STATIC
  format.cpp
  clip.cpp
  library.cpp
  selector.cpp
)
add_library(football::animation_base ALIAS football_animation_base)
target_sources(football_animation_base PUBLIC FILE_SET HEADERS
  BASE_DIRS ${PROJECT_SOURCE_DIR}/include
  FILES
    ${PROJECT_SOURCE_DIR}/include/football/animation/types.hpp
    ${PROJECT_SOURCE_DIR}/include/football/animation/format.hpp
    ${PROJECT_SOURCE_DIR}/include/football/animation/clip.hpp
    ${PROJECT_SOURCE_DIR}/include/football/animation/library.hpp
    ${PROJECT_SOURCE_DIR}/include/football/animation/selector.hpp
    ${PROJECT_SOURCE_DIR}/include/football/animation/selection_math.hpp
    ${PROJECT_SOURCE_DIR}/include/football/animation/selection_query.hpp
    ${PROJECT_SOURCE_DIR}/include/football/animation/quadrant.hpp)
target_compile_features(football_animation_base PUBLIC cxx_std_23)
target_link_libraries(football_animation_base PUBLIC football::foundation)
football_configure_target(football_animation_base)

add_library(football_animation STATIC
  match.cpp
  touch_match.cpp
  movement_selection.cpp
  touch_geometry.cpp
  touch_scoring.cpp
  motion_clip.cpp
  pose_sampling.cpp
  pose_blend.cpp
  playback.cpp
  transition.cpp
  entry_frame.cpp
  trip_match.cpp)
add_library(football::animation ALIAS football_animation)
target_sources(football_animation PUBLIC FILE_SET HEADERS
  BASE_DIRS ${PROJECT_SOURCE_DIR}/include
  FILES
    ${PROJECT_SOURCE_DIR}/include/football/animation/motion_clip.hpp
    ${PROJECT_SOURCE_DIR}/include/football/animation/pose_blend.hpp
    ${PROJECT_SOURCE_DIR}/include/football/animation/pose_sampling.hpp
    ${PROJECT_SOURCE_DIR}/include/football/animation/select_clip.hpp
    ${PROJECT_SOURCE_DIR}/include/football/animation/select_movement.hpp
    ${PROJECT_SOURCE_DIR}/include/football/animation/movement_selection.hpp
    ${PROJECT_SOURCE_DIR}/include/football/animation/movement_math.hpp
    ${PROJECT_SOURCE_DIR}/include/football/animation/match.hpp
    ${PROJECT_SOURCE_DIR}/include/football/animation/touch_match.hpp
    ${PROJECT_SOURCE_DIR}/include/football/animation/touch_geometry.hpp
    ${PROJECT_SOURCE_DIR}/include/football/animation/touch_scoring.hpp
    ${PROJECT_SOURCE_DIR}/include/football/animation/transition.hpp
    ${PROJECT_SOURCE_DIR}/include/football/animation/trip_match.hpp
    ${PROJECT_SOURCE_DIR}/include/football/animation/playback.hpp
    ${PROJECT_SOURCE_DIR}/include/football/animation/entry_frame.hpp)
target_compile_features(football_animation PUBLIC cxx_std_23)
target_link_libraries(football_animation PUBLIC football::animation_base)
football_configure_target(football_animation)
