# Developer diagnostic programs; test-only executables live under test/.
add_executable(football_regression football_regression.cpp)
target_link_libraries(football_regression PRIVATE
  football::game football::app_support football_legacy_anim)
football_configure_target(football_regression)

add_executable(football_restart_metrics football_restart_metrics.cpp)
target_link_libraries(football_restart_metrics PRIVATE
  football::game football::app_support)
football_configure_target(football_restart_metrics)

add_executable(football_body_shadow football_body_shadow.cpp)
target_link_libraries(football_body_shadow PRIVATE football::game football::ai football::app_support)
football_configure_target(football_body_shadow)

add_executable(football_unified_acceptance football_unified_acceptance.cpp)
target_link_libraries(football_unified_acceptance PRIVATE football::game football::ai football::app_support)
football_configure_target(football_unified_acceptance)
