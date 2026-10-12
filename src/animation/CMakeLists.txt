# Header-only baked-animation format + selection.
#
# Independent of sim/player/rules/runtime: the animation module owns baked
# resources and sampling only. The runtime and the offline baker share one
# header-only definition; the baker therefore never links a runtime archive.
# Public headers live in include/football/animation/ (consumed as
# football/animation/<name>.hpp); this directory only declares the target.
add_library(football_animation INTERFACE)
add_library(football::animation ALIAS football_animation)
target_sources(football_animation INTERFACE FILE_SET HEADERS
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
target_compile_features(football_animation INTERFACE cxx_std_23)
target_link_libraries(football_animation INTERFACE football::foundation)
