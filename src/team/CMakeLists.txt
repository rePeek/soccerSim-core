# Static team-domain entity and its pure configuration helpers. This target must
# never depend on Runtime, Referee, AI or concrete Player/Ball implementations.
add_library(football_team STATIC
  team.cpp
  formation_preset.cpp)
add_library(football::team ALIAS football_team)
target_sources(football_team PUBLIC FILE_SET HEADERS
  BASE_DIRS ${PROJECT_SOURCE_DIR}/include
  FILES
    ${PROJECT_SOURCE_DIR}/include/football/team/team.hpp
    ${PROJECT_SOURCE_DIR}/include/football/team/lineup.hpp
    ${PROJECT_SOURCE_DIR}/include/football/team/leadership.hpp
    ${PROJECT_SOURCE_DIR}/include/football/team/set_piece_orders.hpp
    ${PROJECT_SOURCE_DIR}/include/football/team/selection.hpp
    ${PROJECT_SOURCE_DIR}/include/football/team/formation.hpp
    ${PROJECT_SOURCE_DIR}/include/football/team/formation_preset.hpp)
target_link_libraries(football_team
  PUBLIC football::foundation football::pitch)
football_configure_target(football_team)
