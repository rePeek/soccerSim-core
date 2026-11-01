# Player action decisions: candidate refinement, movement pick, touch matching,
# trip/reaction matching and entry frames. These are Player-owned decisions over
# the shared clip library, so the suite links Player Execution and needs the
# baked asset cache. Animation itself is used as a read-only resource.

add_executable(football_player_decision_tests
  player_action_resolver_test.cpp
  player_action_candidates_test.cpp
  player_touch_action_resolver_test.cpp
  player_touch_action_scoring_test.cpp
  player_reaction_resolver_test.cpp
  player_entry_frame_test.cpp
  select_movement_test.cpp)
target_include_directories(football_player_decision_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/../animation)
football_require_animation_cache(football_player_decision_tests)
target_link_libraries(football_player_decision_tests
  PRIVATE
    football::player_execution
    football::animation
    Catch2::Catch2WithMain)
football_configure_target(football_player_decision_tests)

add_test(NAME player_decision COMMAND football_player_decision_tests)
set_tests_properties(player_decision PROPERTIES
  LABELS "unit;player;decision"
  TIMEOUT 30)
