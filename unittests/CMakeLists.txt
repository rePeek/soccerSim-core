# Unit suites for the modules currently under development.
#
# Scope is deliberate: only the leaf module suites plus the two Player link
# proofs and the Animation acceptance suites run here. The former
# foundation/model/ai/collision/contact/event/referee/public-header suites and
# the bounded runtime harness (unittests/runtime, including its 22-player
# real-tick cases) are not part of this scope and were removed; restore them
# from history when those modules are back under active work.
#
# Every suite here links only the module under test and its declared
# dependencies. Nothing constructs a full Simulation or plays a match.

add_subdirectory(animation)
add_subdirectory(pitch)
add_subdirectory(ball)
add_subdirectory(team)
add_subdirectory(team_link)
add_subdirectory(player)
add_subdirectory(player_link)
add_subdirectory(player_animation)
add_subdirectory(locomotion)
add_subdirectory(loco_animation)
