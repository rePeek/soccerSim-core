# LOCO-2.0 Movement animation catalog audit. Loads the baked animation asset
# (isolated here so the pure-math player target stays asset-free) and classifies
# Movement clips. Links only football::animation + Catch2.

add_executable(football_player_animation_catalog_tests
  player_animation_catalog_test.cpp)
football_require_animation_cache(football_player_animation_catalog_tests)
target_link_libraries(football_player_animation_catalog_tests
  PRIVATE
    football::animation
    Catch2::Catch2WithMain)

football_configure_target(football_player_animation_catalog_tests)

add_test(NAME player_animation_catalog COMMAND football_player_animation_catalog_tests)
set_tests_properties(player_animation_catalog PROPERTIES
  LABELS "unit;player;animation"
  TIMEOUT 10)