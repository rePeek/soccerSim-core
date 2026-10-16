# ANIM-2 A10.7: the Animation module's own archive.
#
# Only the Match entry point has an out-of-line definition. The format and
# selection headers stay header-only (football::animation) and are still shared
# with the offline baker, which must not link a runtime archive.
add_library(football_animation_match STATIC match.cpp)
target_link_libraries(football_animation_match PUBLIC football::animation)
football_configure_target(football_animation_match)
