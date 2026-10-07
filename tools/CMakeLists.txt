# Developer diagnostic programs; test-only executables live under test/.
add_executable(football_regression football_regression.cpp)
target_link_libraries(football_regression PRIVATE
  football::game football::app_support football_legacy_anim)
football_configure_target(football_regression)

add_executable(football_restart_metrics football_restart_metrics.cpp)
target_link_libraries(football_restart_metrics PRIVATE
  football::game football::app_support)
football_configure_target(football_restart_metrics)
