cmake_minimum_required(VERSION 3.24)

add_library(football_support STATIC
  config/properties.cpp
  config/properties.hpp
  diagnostics/assert.hpp
  diagnostics/backtrace.cpp
  diagnostics/backtrace.hpp
  diagnostics/log.cpp
  diagnostics/log.hpp
  io/file.cpp
  io/file.hpp
  io/xml_loader.cpp
  io/xml_loader.hpp
  text/string_utils.cpp
  text/string_utils.hpp
  text/value_codec.cpp
  text/value_codec.hpp
)

add_library(football::support ALIAS football_support)
target_include_directories(football_support PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>)
target_compile_features(football_support PUBLIC cxx_std_23)
target_link_libraries(football_support PUBLIC football_foundation)
set_target_properties(football_support PROPERTIES POSITION_INDEPENDENT_CODE ON)
