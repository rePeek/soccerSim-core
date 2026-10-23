# Independent Referee rule module: value rule facts in, RefereeRuling out. It
# links only the public value contracts and never Player, Event or Runtime
# internals; Runtime owns every actor/ball mutation and restart execution.
add_library(football_referee STATIC
  action_authorization.cpp
  ball_rules.cpp
  goal.cpp
  offside.cpp
  foul_sanction.cpp
  period.cpp
  referee.cpp)
add_library(football::referee ALIAS football_referee)
target_include_directories(football_referee PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_include_directories(football_referee PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(football_referee PUBLIC football::foundation football::pitch)
football_configure_target(football_referee)

# The Runtime composition depends on the referee rule library.
target_link_libraries(football_sim_domain PRIVATE football::referee)
