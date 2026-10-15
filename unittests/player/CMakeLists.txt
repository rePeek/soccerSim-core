# Player body model unit tests: pure PlayerBodyProfile derivation, static
# bind-pose geometry (nine height/build profiles) and the headless pitch
# placement validation (BODY-1.8) that places standing-pose players on a real
# Pitch. Links football::collision/animation/pitch for geometry + foundation
# and includes the private src/ tree; never football::sim, actors, RNG,
# animation assets or a running match.

add_executable(football_player_unit_tests
  player_body_profile_test.cpp
  player_body_geometry_test.cpp
  player_body_placement_test.cpp
  player_locomotion_animation_test.cpp
  locomotion_animation_strategy_test.cpp
  animation_transition_state_test.cpp
  player_action_request_test.cpp
  player_playback_state_test.cpp)
target_include_directories(football_player_unit_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/../animation)
target_link_libraries(football_player_unit_tests
  PRIVATE
    football::collision
    football::animation
    football::pitch
    Catch2::Catch2WithMain)

football_configure_target(football_player_unit_tests)

add_test(NAME player_unit COMMAND football_player_unit_tests)
set_tests_properties(player_unit PROPERTIES
  LABELS "unit;player"
  TIMEOUT 30)