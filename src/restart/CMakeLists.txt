# Restart execution: the in-flight restart state machine, its planner, the
# kickoff placement ceremony and the rule command port. Referee awards a
# restart; this module executes the confirmed award without owning the rules.
add_library(football_restart STATIC
  restart_context.hpp
  restart_execution_context.hpp
  restart_planner.cpp restart_planner.hpp
  restart_controller.cpp restart_controller.hpp
  restart_placement.cpp restart_placement.hpp
  rule_command_sink.hpp)
add_library(football::restart ALIAS football_restart)
target_include_directories(football_restart PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(football_restart
  PUBLIC football::sim_contracts football::team
  PRIVATE
    football::player football::ball football::pitch football::foundation
    football::referee)
football_configure_target(football_restart)
