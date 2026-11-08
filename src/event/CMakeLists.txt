# Event module implementation. Independent of Runtime: it recognises behavior
# from value facts and the observation history, and is linked by the Runtime
# composition rather than contributing sources to it.
add_library(football_event STATIC
  accepted_touch.hpp
  accepted_touch_sink.hpp
  event_log.hpp
  event_recognizer.cpp event_recognizer.hpp
  event_transition.hpp
  rule_touch.hpp
  touch_query.hpp
  touch_record.hpp
  touch_state.hpp
  touch_type.hpp)
add_library(football::event ALIAS football_event)
target_include_directories(football_event PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(football_event
  PUBLIC football::sim_contracts
  PRIVATE football::observation football::foundation)
football_configure_target(football_event)
