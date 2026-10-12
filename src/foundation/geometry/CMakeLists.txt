# football::geometry — shared shape primitives and the per-tick collider
# motion/material contract. Header-only; depends only on foundation math.
add_library(football_geometry INTERFACE)
add_library(football::geometry ALIAS football_geometry)
target_sources(football_geometry INTERFACE FILE_SET HEADERS
  BASE_DIRS ${PROJECT_SOURCE_DIR}/include
  FILES
    ${PROJECT_SOURCE_DIR}/include/football/foundation/geometry/shapes.hpp
    ${PROJECT_SOURCE_DIR}/include/football/foundation/geometry/collider.hpp)
target_compile_features(football_geometry INTERFACE cxx_std_23)
target_link_libraries(football_geometry INTERFACE football::foundation)
