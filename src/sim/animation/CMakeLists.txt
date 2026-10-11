# Header-only baked-animation format + selection.
#
# The runtime and the offline baker share one header-only definition; the baker
# therefore never links a runtime archive. Keep the format independent of sim,
# ball, pitch and game.
add_library(football_animation INTERFACE)
add_library(football::animation ALIAS football_animation)
target_sources(football_animation INTERFACE FILE_SET HEADERS
  BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/../..
  FILES types.hpp selection_math.hpp selection_query.hpp quadrant.hpp
        simanim_format.hpp clip.hpp library.hpp baked_selector.hpp)
target_compile_features(football_animation INTERFACE cxx_std_23)
target_link_libraries(football_animation INTERFACE football::foundation)
