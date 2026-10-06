# Independent baked-runtime archive, also consumed by the offline baker.
add_library(football_animation STATIC)
add_library(football::animation ALIAS football_animation)
target_sources(football_animation
  PRIVATE library.cpp baked_selector.cpp
  PUBLIC FILE_SET HEADERS
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/../..
    FILES types.hpp selection_math.hpp selection_query.hpp quadrant.hpp
          simanim_format.hpp clip.hpp library.hpp baked_selector.hpp)
target_link_libraries(football_animation PUBLIC football::foundation)
football_configure_target(football_animation)
