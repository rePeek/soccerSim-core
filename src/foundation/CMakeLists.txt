cmake_minimum_required(VERSION 3.24)

# This directory may be consumed either with:
#
#   add_subdirectory(path/to/src/foundation)
#
# or configured directly for a standalone foundation build.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(football_foundation LANGUAGES CXX)
endif()

add_library(football_foundation STATIC)
add_library(football::foundation ALIAS football_foundation)
target_sources(football_foundation
  PRIVATE
    geometry/line.cpp geometry/triangle.cpp
    math/matrix3.cpp math/matrix4.cpp math/quaternion.cpp math/scalar.cpp math/vector3.cpp
  PUBLIC FILE_SET HEADERS
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/..
    FILES
      geometry/line.hpp geometry/triangle.hpp
      math/matrix3.hpp math/matrix4.hpp math/quaternion.hpp math/rng.hpp
      math/scalar.hpp math/vector3.hpp
      time/tick.hpp time/tick_boundary.hpp)
target_compile_features(football_foundation PUBLIC cxx_std_23)

# The library is linked into libgame.so in the parent project. Keep this local
# so add_subdirectory() works even when the parent does not set global PIC.
set_target_properties(football_foundation PROPERTIES
  POSITION_INDEPENDENT_CODE ON)
