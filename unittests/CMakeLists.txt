# Small, fast unit suites for the leaf modules.
#
# They link only the module under test (and its declared dependencies); never
# football::simcore, football::ai, player/animation assets or a running match.

add_subdirectory(foundation)
