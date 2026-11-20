# Domain suites link only their modules; C ABI tests link the shared library.
add_subdirectory(collision)
add_subdirectory(pitch)
add_subdirectory(ball)
# Env is compiled into simcore, so its suite links the shared library too.
add_subdirectory(env)
add_subdirectory(runner)
add_subdirectory(capi)
