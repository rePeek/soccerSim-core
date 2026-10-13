# Player static body model: PlayerBodyProfile derivation from height/weight.
# Pure value computation; links only Catch2 and includes the private src/ tree,
# never football::sim, actors, RNG or animation assets.

add_executable(football_player_unit_tests player_body_profile_test.cpp)
target_include_directories(football_player_unit_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(football_player_unit_tests
  PRIVATE
    Catch2::Catch2WithMain)

football_configure_target(football_player_unit_tests)

add_test(NAME player_unit COMMAND football_player_unit_tests)
set_tests_properties(player_unit PROPERTIES
  LABELS "unit;player"
  TIMEOUT 10)