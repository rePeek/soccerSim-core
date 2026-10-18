# Small unit suites for the leaf modules, plus one bounded runtime harness.
#
# The leaf suites link only the module under test (and its declared
# dependencies); never football::simcore, football::ai, player/animation assets
# or a running match. The runtime/ harness links the sim domain and runs a
# handful of ticks under a wall-clock CTest timeout -- it never plays a match.

add_subdirectory(foundation)
add_subdirectory(public_header)
add_subdirectory(ai)
add_subdirectory(animation)
add_subdirectory(collision)
add_subdirectory(pitch)
add_subdirectory(ball)
add_subdirectory(team)
add_subdirectory(runtime)
add_subdirectory(player)
add_subdirectory(player_link)
add_subdirectory(player_animation)
add_subdirectory(locomotion)
add_subdirectory(loco_animation)
