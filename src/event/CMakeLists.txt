# Event module implementation. Declares only its sources on the shared
# football_sim_domain target; the public trajectory values live in
# include/football/event/, the rest stays internal.
target_sources(football_sim_domain PRIVATE
  accepted_touch.hpp
  accepted_touch_sink.hpp
  event_log.hpp
  event_recognizer.cpp event_recognizer.hpp
  event_transition.hpp
  match_event.hpp
  rule_touch.hpp
  touch_query.hpp
  touch_record.hpp
  touch_state.hpp
  touch_type.hpp)
