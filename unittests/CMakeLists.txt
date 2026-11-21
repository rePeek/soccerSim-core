# Domain suites link only their modules; C ABI tests link the shared library.
add_subdirectory(collision)
add_subdirectory(pitch)
add_subdirectory(ball)
add_subdirectory(history)
# Env and the runner are compiled into simcore, so their suites link the shared
# library too.
add_subdirectory(env)
add_subdirectory(runner)
add_subdirectory(capi)
