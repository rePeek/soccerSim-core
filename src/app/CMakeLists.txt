# These archives remain available with CLI/tests disabled for value-only consumers.
# EXCLUDE_FROM_ALL keeps executable-side state/fixtures out of core-only builds.
add_library(football_app_support STATIC EXCLUDE_FROM_ALL)
add_library(football::app_support ALIAS football_app_support)
target_sources(football_app_support
  PRIVATE args.cpp fixtures/legacy_player_profile.cpp fixtures/default_teams.cpp
  PUBLIC FILE_SET HEADERS
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/..
    FILES args.hpp fixtures/legacy_player_profile.hpp fixtures/default_teams.hpp)
target_link_libraries(football_app_support PUBLIC football::support football::model)
football_configure_target(football_app_support)

add_library(football_app_input STATIC EXCLUDE_FROM_ALL)
add_library(football::app_input ALIAS football_app_input)
target_sources(football_app_input
  PRIVATE input/grf/input.cpp
  PUBLIC FILE_SET HEADERS
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/..
    FILES input/grf/action.hpp input/grf/input.hpp)
target_link_libraries(football_app_input PUBLIC football::sim_contracts)
football_configure_target(football_app_input)

if(FOOTBALL_BUILD_APP)
  add_executable(football_app app.cpp)
  target_link_libraries(football_app PRIVATE football::game football::app_support)
  football_configure_target(football_app)
endif()
