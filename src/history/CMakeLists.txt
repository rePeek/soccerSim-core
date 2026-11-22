add_library(football_history STATIC)
add_library(football::history ALIAS football_history)

target_sources(football_history
  PRIVATE
    history.cpp
  PUBLIC FILE_SET HEADERS
    BASE_DIRS ${PROJECT_SOURCE_DIR}/include
    FILES
      ${PROJECT_SOURCE_DIR}/include/football/history/snapshot.hpp
      ${PROJECT_SOURCE_DIR}/include/football/history/chunk.hpp
      ${PROJECT_SOURCE_DIR}/include/football/history/history.hpp)

target_compile_features(football_history PUBLIC cxx_std_23)

target_link_libraries(football_history
  PUBLIC
    football::ball_state)

football_configure_target(football_history)
