# ANIM-2 A12: the Animation module owns two archives.
#
# football::animation_base — immutable clips and canonical catalog import,
# including private support analysis, gait repair and quality measurements.
# Candidate selection and Player ability/touch policy remain in Player.
#
# football::animation — pure runtime reference/pose sampling, geometry,
# blending, playback and transitions on top of the resource target.
#
# Both are declared here (not in the root CMakeLists) so the module owns its
# targets, sources and header sets.

# Imports and runtime algorithms may depend only on lower-level contracts.
file(GLOB_RECURSE _animation_boundary_files CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
  ${PROJECT_SOURCE_DIR}/include/football/animation/*.hpp)
foreach(_file IN LISTS _animation_boundary_files)
  file(READ "${_file}" _content)
  if(_content MATCHES "#[ \t]*include[ \t]*[<\"](football/)?(player|runtime|team|ball)/")
    message(FATAL_ERROR "Animation boundary violation: ${_file} includes an upper-layer header")
  endif()
endforeach()

add_library(football_animation_base STATIC
  import/catalog_loader.cpp
  import/glb_reader.cpp
  import/metadata.cpp
  import/mirror.cpp
  import/procedural_generator.cpp
  import/gait_repair.cpp
  import/motion_quality.cpp
)
add_library(football::animation_base ALIAS football_animation_base)
target_sources(football_animation_base PUBLIC FILE_SET HEADERS
  BASE_DIRS ${PROJECT_SOURCE_DIR}/include
  FILES
    ${PROJECT_SOURCE_DIR}/include/football/animation/types.hpp
    ${PROJECT_SOURCE_DIR}/include/football/animation/clip.hpp
    ${PROJECT_SOURCE_DIR}/include/football/animation/library.hpp
    ${PROJECT_SOURCE_DIR}/include/football/animation/quadrant.hpp
    ${PROJECT_SOURCE_DIR}/include/football/animation/skeleton.hpp)
target_compile_features(football_animation_base PUBLIC cxx_std_23)
target_link_libraries(football_animation_base PUBLIC football::foundation
  PRIVATE football_cgltf nlohmann_json::nlohmann_json)
football_configure_target(football_animation_base)

add_library(football_animation STATIC
  reference_sample.cpp
  touch_geometry.cpp
  motion_clip.cpp
  pose_sampling.cpp
  pose_blend.cpp
  playback.cpp
  transition.cpp
  entry_frame.cpp)
add_library(football::animation ALIAS football_animation)
target_sources(football_animation PUBLIC FILE_SET HEADERS
  BASE_DIRS ${PROJECT_SOURCE_DIR}/include
  FILES
    ${PROJECT_SOURCE_DIR}/include/football/animation/motion_clip.hpp
    ${PROJECT_SOURCE_DIR}/include/football/animation/pose_blend.hpp
    ${PROJECT_SOURCE_DIR}/include/football/animation/pose_sampling.hpp
    ${PROJECT_SOURCE_DIR}/include/football/animation/movement_math.hpp
    ${PROJECT_SOURCE_DIR}/include/football/animation/selection_math.hpp
    ${PROJECT_SOURCE_DIR}/include/football/animation/touch_geometry.hpp
    ${PROJECT_SOURCE_DIR}/include/football/animation/transition.hpp
    ${PROJECT_SOURCE_DIR}/include/football/animation/playback.hpp
    ${PROJECT_SOURCE_DIR}/include/football/animation/entry_frame.hpp
    ${PROJECT_SOURCE_DIR}/include/football/animation/reference_sample.hpp)
target_compile_features(football_animation PUBLIC cxx_std_23)
target_link_libraries(football_animation PUBLIC football::animation_base)
football_configure_target(football_animation)
