# Thin composition implementation; the public header exposes values, not policy.
add_library(football_engine OBJECT)
add_library(football::env ALIAS football_engine)
target_sources(football_engine
  PRIVATE game_env.cpp
  PUBLIC FILE_SET HEADERS
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/..
    FILES game_env.hpp)
target_link_libraries(football_engine PRIVATE
  football::sim football::ai football::model)
football_configure_target(football_engine)
