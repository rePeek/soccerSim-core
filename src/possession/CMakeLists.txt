# Possession module: per-actor ball relations, their staggered refresh and the
# cross-team arbitration. Runtime owns the instances; this library owns the
# algorithms and the relation/staggering value types.
add_library(football_possession STATIC
  player_ball_table.cpp player_ball_table.hpp
  possession.cpp possession.hpp
  team_possession.cpp team_possession.hpp)
add_library(football::possession ALIAS football_possession)
target_include_directories(football_possession PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(football_possession
  PUBLIC football::team football::foundation
  PRIVATE football::player football::ball)
football_configure_target(football_possession)
