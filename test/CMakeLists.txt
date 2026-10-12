# Test suite placeholder.
#
# All previous cases and test/baselines golden files were removed; the suite is
# being redesigned from scratch. The harness is still wired up: the root
# CMakeLists fetches Catch2 (BUILD_TESTING=ON) and adds this directory, so new
# executables/tests can be registered here.
#
# Example:
#   add_executable(football_example_test example_test.cpp)
#   target_link_libraries(football_example_test PRIVATE
#     football::foundation Catch2::Catch2WithMain)
#   football_configure_target(football_example_test)
#   add_test(NAME football_example_test COMMAND football_example_test)
