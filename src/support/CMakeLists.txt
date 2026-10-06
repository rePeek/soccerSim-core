cmake_minimum_required(VERSION 3.24)

add_library(football_support STATIC)
add_library(football::support ALIAS football_support)
target_sources(football_support
  PRIVATE diagnostics/backtrace.cpp diagnostics/log.cpp io/file.cpp
  PUBLIC FILE_SET HEADERS
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/..
    FILES diagnostics/assert.hpp diagnostics/backtrace.hpp
      diagnostics/log.hpp io/file.hpp)
target_compile_features(football_support PUBLIC cxx_std_23)
target_link_libraries(football_support PUBLIC football::foundation)
set_target_properties(football_support PROPERTIES POSITION_INDEPENDENT_CODE ON)
