# ANIM-2 A11.4: the runtime Animation archive.
#
# Match, TouchMatch and the playback/transition state compile here; the format
# and pure-algorithm headers stay in football::animation_format and are shared
# with the offline baker, which never links this archive.
add_library(football_animation STATIC match.cpp touch_match.cpp)
add_library(football::animation ALIAS football_animation)
target_sources(football_animation PUBLIC FILE_SET HEADERS
  BASE_DIRS ${PROJECT_SOURCE_DIR}/include
  FILES
    ${PROJECT_SOURCE_DIR}/include/football/animation/match.hpp
    ${PROJECT_SOURCE_DIR}/include/football/animation/touch_match.hpp
    ${PROJECT_SOURCE_DIR}/include/football/animation/transition.hpp
    ${PROJECT_SOURCE_DIR}/include/football/animation/playback.hpp)
target_compile_features(football_animation PUBLIC cxx_std_23)
target_link_libraries(football_animation PUBLIC football::animation_format)
football_configure_target(football_animation)
