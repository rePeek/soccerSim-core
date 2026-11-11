# football::log owns the shared logger and the per-component channels.
# Domain modules log through it instead of including BqLog directly.
add_library(football_log STATIC log.cpp)
add_library(football::log ALIAS football_log)

target_sources(football_log PUBLIC FILE_SET HEADERS
  BASE_DIRS ${PROJECT_SOURCE_DIR}/include
  FILES
    ${PROJECT_SOURCE_DIR}/include/football/log/log.hpp)

target_link_libraries(football_log PRIVATE BqLog)
football_configure_target(football_log)
