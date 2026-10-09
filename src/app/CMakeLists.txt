# Executable-side args/fixtures remain available with CLI/tests disabled.
# EXCLUDE_FROM_ALL keeps sample inputs out of core-only builds.
add_library(football_app_support STATIC EXCLUDE_FROM_ALL)
add_library(football::app_support ALIAS football_app_support)
target_sources(football_app_support
  PRIVATE args.cpp fixtures/legacy_player_profile.cpp fixtures/default_teams.cpp
  PUBLIC FILE_SET HEADERS
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/..
    FILES args.hpp fixtures/legacy_player_profile.hpp fixtures/default_teams.hpp)
target_link_libraries(football_app_support
  PUBLIC football::model PRIVATE football::foundation)
football_configure_target(football_app_support)

# Disk I/O and the private writer thread remain executable-side, never libgame.
find_package(Threads REQUIRED)
add_library(football_recording STATIC EXCLUDE_FROM_ALL recording/snapshot_archive.cpp)
add_library(football::recording ALIAS football_recording)
target_sources(football_recording PUBLIC FILE_SET HEADERS
  BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/..
  FILES recording/snapshot_archive.hpp)
target_link_libraries(football_recording
  PUBLIC football::sim_contracts PRIVATE Threads::Threads)
football_configure_target(football_recording)

if(FOOTBALL_BUILD_APP)
  add_executable(football_app app.cpp)
  target_link_libraries(football_app PRIVATE football::game football::app_support football::recording)
  football_configure_target(football_app)
endif()
