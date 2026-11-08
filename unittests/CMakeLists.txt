# Unit suites for the modules currently under development, plus the bounded
# runtime regression harness. Leaf suites link only the module under test; the
# runtime suite is the match-level gate used while Runtime is decomposed.
add_subdirectory(animation)
add_subdirectory(pitch)
add_subdirectory(ball)
add_subdirectory(team)
add_subdirectory(team_link)
add_subdirectory(player)
add_subdirectory(player_decision)
add_subdirectory(player_link)
add_subdirectory(player_animation)
add_subdirectory(locomotion)
add_subdirectory(loco_animation)

if(FOOTBALL_BUILD_RUNTIME)
  add_subdirectory(runtime)
endif()
