# Developer diagnostic program; test-only executables live under test/.
add_executable(football_regression football_regression.cpp)
target_link_libraries(football_regression PRIVATE
  football::game football::app_support football_legacy_anim)
football_configure_target(football_regression)
