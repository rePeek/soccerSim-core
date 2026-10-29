# Static team-domain entity and its configuration helpers. The Team entity owns
# the concrete Player objects, so this archive links football::player; it never
# depends on Runtime, Referee or AI.
add_library(football_team STATIC
  team.cpp
  formation_preset.cpp
  team_players.cpp)
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
    ${PROJECT_SOURCE_DIR}/include/football/team/formation_preset.hpp
    ${PROJECT_SOURCE_DIR}/include/football/team/formation_entry.hpp)
target_link_libraries(football_team
  PUBLIC football::player football::foundation football::pitch)
# team_players.cpp implements the Player-owning entity and needs the concrete
# Player internal header; the edge is Team -> Player only.
target_include_directories(football_team PRIVATE ${PROJECT_SOURCE_DIR}/src)
football_configure_target(football_team)
