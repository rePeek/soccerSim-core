cmake_minimum_required(VERSION 3.24)

# football::foundation — math/time primitives. Public headers live in the
# top-level include/ tree; this directory holds only the implementation. It is
# built from the repository root (PROJECT_SOURCE_DIR/include).

add_library(football_foundation STATIC)
add_library(football::foundation ALIAS football_foundation)
target_sources(football_foundation
  PRIVATE
    math/quaternion.cpp math/scalar.cpp math/vector3.cpp
  PUBLIC FILE_SET HEADERS
    BASE_DIRS ${PROJECT_SOURCE_DIR}/include
    FILES
      ${PROJECT_SOURCE_DIR}/include/football/foundation/math/quaternion.hpp
      ${PROJECT_SOURCE_DIR}/include/football/foundation/math/scalar.hpp
      ${PROJECT_SOURCE_DIR}/include/football/foundation/math/vector3.hpp
      ${PROJECT_SOURCE_DIR}/include/football/foundation/time/tick.hpp
      ${PROJECT_SOURCE_DIR}/include/football/foundation/time/tick_boundary.hpp)
target_compile_features(football_foundation PUBLIC cxx_std_23)

# The library is linked into libsimcore.so in the parent project. Keep this local
# so add_subdirectory() works even when the parent does not set global PIC.
set_target_properties(football_foundation PROPERTIES
  POSITION_INDEPENDENT_CODE ON)

# geometry and collision live under foundation/ but are independent targets.
# football_foundation must never link them back: geometry depends on
# foundation, so a return edge would create a target cycle.
#
# geometry is header-only, so its target is declared here (no separate
# directory with only a CMakeLists).
add_library(football_geometry INTERFACE)
add_library(football::geometry ALIAS football_geometry)
target_sources(football_geometry INTERFACE FILE_SET HEADERS
  BASE_DIRS ${PROJECT_SOURCE_DIR}/include
  FILES
    ${PROJECT_SOURCE_DIR}/include/football/foundation/geometry/shapes.hpp
    ${PROJECT_SOURCE_DIR}/include/football/foundation/geometry/collider.hpp)
target_compile_features(football_geometry INTERFACE cxx_std_23)
target_link_libraries(football_geometry INTERFACE football::foundation)

add_subdirectory(collision)
