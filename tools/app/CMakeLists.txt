# Executable-side args/fixtures are internal support for the football_sim CLI;
# EXCLUDE_FROM_ALL keeps them out of core-only builds.
add_library(football_app_support STATIC EXCLUDE_FROM_ALL)
add_library(football::app_support ALIAS football_app_support)
target_sources(football_app_support
  PRIVATE args.cpp fixtures/legacy_player_profile.cpp fixtures/default_teams.cpp
  PUBLIC FILE_SET HEADERS
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/..
    FILES args.hpp fixtures/legacy_player_profile.hpp fixtures/default_teams.hpp)
# Fixtures build sim::TeamSetup/player definitions, so they need the sim
# value-contract include root as well as foundation.
target_link_libraries(football_app_support
  PUBLIC football::sim_contracts)
football_configure_target(football_app_support)

add_executable(football_sim app.cpp)
target_link_libraries(football_sim PRIVATE football::simcore football::app_support)
football_configure_target(football_sim)
