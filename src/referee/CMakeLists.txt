# Referee module implementation. Declares only its sources on the shared
# football_sim_domain target; the module's public enum values live in
# include/football/referee/, everything else stays internal.
target_sources(football_sim_domain PRIVATE
  action_authorization.cpp action_authorization.hpp
  ball_rules.cpp ball_rules.hpp
  ball_touch_facts.hpp
  goal.cpp goal.hpp
  offside.cpp offside.hpp
  period.cpp period.hpp
  referee.cpp referee.hpp
  referee_state.hpp
  referee_tick_facts.hpp
  referee_view.hpp
  rule_command_sink.hpp
  ruling.hpp)
