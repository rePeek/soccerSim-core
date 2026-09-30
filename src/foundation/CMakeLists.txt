cmake_minimum_required(VERSION 3.24)

# This directory may be consumed either with:
#
#   add_subdirectory(path/to/src/foundation)
#
# or configured directly for a standalone foundation build.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(football_foundation LANGUAGES CXX)
endif()

add_library(football_foundation STATIC
  backtrace.cpp
  backtrace.h
  defines.hpp
  file.cpp
  file.h
  log.cpp
  log.hpp
  properties.cpp
  properties.hpp
  utils.cpp
  utils.hpp
  xml_loader.cpp
  xml_loader.hpp

  geometry/line.cpp
  geometry/line.hpp
  geometry/triangle.cpp
  geometry/triangle.hpp

  math/bluntmath.cpp
  math/bluntmath.hpp
  math/matrix3.cpp
  math/matrix3.hpp
  math/matrix4.cpp
  math/matrix4.hpp
  math/quaternion.cpp
  math/quaternion.hpp
  math/rng.hpp
  math/vector3.cpp
  math/vector3.hpp

  misc/hungarian.cpp
  misc/hungarian.h
)

add_library(football::foundation ALIAS football_foundation)

# Foundation headers are included as "foundation/...", so consumers need the
# src directory rather than this directory itself on their include path.
target_include_directories(football_foundation PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>)
target_compile_features(football_foundation PUBLIC cxx_std_23)

# The library is linked into libgame.so in the parent project. Keep this local
# so add_subdirectory() works even when the parent does not set global PIC.
set_target_properties(football_foundation PROPERTIES
  POSITION_INDEPENDENT_CODE ON)
