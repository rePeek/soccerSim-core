# Small unit suites for the leaf modules, plus one bounded runtime harness.
#
# The leaf suites link only the module under test (and its declared
# dependencies); never football::simcore, football::ai, player/animation assets
# or a running match. The runtime/ harness links the sim domain and runs a
# handful of ticks under a wall-clock CTest timeout -- it never plays a match.

add_subdirectory(foundation)
add_subdirectory(public_header)
add_subdirectory(model)
add_subdirectory(ai)
add_subdirectory(animation)
add_subdirectory(collision)
add_subdirectory(pitch)
add_subdirectory(ball)
add_subdirectory(team)
add_subdirectory(event)
add_subdirectory(referee)
add_subdirectory(runtime)
add_subdirectory(player)
add_subdirectory(player_link)
add_subdirectory(player_animation)
add_subdirectory(locomotion)
add_subdirectory(loco_animation)

# Full autonomous-match acceptance. Release-only by default so the fast Debug
# CTest budget is untouched; force it on anywhere with
# -DFOOTBALL_INTEGRATION_TESTS=ON.
set(FOOTBALL_INTEGRATION_TESTS_DEFAULT OFF)
if(CMAKE_BUILD_TYPE STREQUAL "Release" OR
   CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo" OR
   CMAKE_CXX_FLAGS MATCHES "-O[23]")
  set(FOOTBALL_INTEGRATION_TESTS_DEFAULT ON)
endif()
option(FOOTBALL_INTEGRATION_TESTS
       "Build the full-match integration acceptance tests"
       ${FOOTBALL_INTEGRATION_TESTS_DEFAULT})
if(FOOTBALL_INTEGRATION_TESTS)
  add_subdirectory(integration)
endif()

# Optional deterministic full-roster acceptance (ANIM-2 A17). Off by default
# so routine module and interface work never pays for it.
option(FOOTBALL_DETERMINISM_TESTS
       "Register the optional deterministic full-roster acceptance test" OFF)
