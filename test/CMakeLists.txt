# Catch2 suites for the executable side of the project: the CLI argument
# parsing, the app fixtures/importer, and the exact composition the CLI runs.
# These link the app fixtures and the core shared library but never become
# part of the core itself.

add_executable(football_app_test
  app_args_test.cpp
  app_fixtures_test.cpp
  app_cli_test.cpp)
target_link_libraries(football_app_test PRIVATE
  football::app_support football::game)
# Keep the Catch2 target out of the archive-order chain: it is an interface
# target for the test executable only.
target_link_libraries(football_app_test PRIVATE Catch2::Catch2WithMain)
football_configure_target(football_app_test)

add_test(NAME football_app_test COMMAND football_app_test)
set_tests_properties(football_app_test PROPERTIES LABELS app)

# The real CLI always runs to the referee's final whistle; no external tick budget.
if(TARGET football_app)
  add_test(NAME football_app_cli_default COMMAND football_app)
  set_tests_properties(football_app_cli_default PROPERTIES
    PASS_REGULAR_EXPRESSION "home_score=[0-9]+ away_score=[0-9]+ outcome=.* duration_ticks=[1-9][0-9]*"
    # Two real 45-minute halves at 100 Hz, also with native Debug assertions.
    TIMEOUT 900)
  add_test(NAME football_app_cli_short_match COMMAND football_app --half-duration-ms=1800)
  set_tests_properties(football_app_cli_short_match PROPERTIES
    PASS_REGULAR_EXPRESSION "home_score=0 away_score=0 outcome=draw duration_ticks=[1-9][0-9]*")
  foreach(value IN ITEMS abc -5 0 1 11 18446744073709551615)
    add_test(NAME football_app_cli_rejects_duration_${value}
      COMMAND football_app --half-duration-ms=${value})
    set_tests_properties(football_app_cli_rejects_duration_${value} PROPERTIES WILL_FAIL TRUE)
  endforeach()
  add_test(NAME football_app_cli_rejects_tick_budget COMMAND football_app --steps=10)
  set_tests_properties(football_app_cli_rejects_tick_budget PROPERTIES WILL_FAIL TRUE)
endif()


# Runtime computation ownership has its own focused tests. App descriptions
# remain test inputs and never become a runtime dependency.
add_executable(football_sim_computation_test sim_computation_test.cpp tick_test.cpp
  player_action_tick_test.cpp prediction_tick_test.cpp player_observation_tick_test.cpp
  reachability_tick_test.cpp ball_player_contact_test.cpp player_contact_test.cpp
  player_body_collider_motion_test.cpp player_body_shadow_test.cpp
  player_body_contact_diagnostics_test.cpp player_active_touch_shadow_test.cpp
  player_active_touch_model_test.cpp
  possession_test.cpp mental_image_history_test.cpp referee_state_test.cpp
  sim_fact_test.cpp event_recognizer_test.cpp rule_query_test.cpp
  touch_inference_test.cpp rule_command_fixture.hpp
  referee_process_fixture.hpp)
target_link_libraries(football_sim_computation_test PRIVATE
  football::game football::ai football::app_support Catch2::Catch2WithMain)
football_configure_target(football_sim_computation_test)
add_test(NAME football_sim_computation_test COMMAND football_sim_computation_test)
set_tests_properties(football_sim_computation_test PROPERTIES LABELS sim)

# Pure goal geometry must compile/link without Match, Ball, sim or libgame.
add_executable(football_goal_test goal_test.cpp ../src/sim/referee/goal.cpp)
target_link_libraries(football_goal_test PRIVATE
  football::foundation football::model Catch2::Catch2WithMain)
football_configure_target(football_goal_test)
add_test(NAME football_goal_test COMMAND football_goal_test)
set_tests_properties(football_goal_test PROPERTIES LABELS sim)

# Clock invariants need only match/time values, never concrete actors or sim runtime.
add_executable(football_match_clock_test match_clock_test.cpp ../src/sim/runtime/clock.cpp)
target_link_libraries(football_match_clock_test PRIVATE
  football::sim_contracts Catch2::Catch2WithMain)
football_configure_target(football_match_clock_test)
add_test(NAME football_match_clock_test COMMAND football_match_clock_test)
set_tests_properties(football_match_clock_test PROPERTIES LABELS sim)

# Snapshot storage is tested independently of actors, Simulation and libgame.
# Its test-local allocation counter must not interpose on other runtime suites.
add_executable(football_snapshot_history_test snapshot_history_test.cpp
  ../src/sim/observation/snapshot_history.cpp)
target_link_libraries(football_snapshot_history_test PRIVATE
  football::sim_contracts Catch2::Catch2WithMain)
football_configure_target(football_snapshot_history_test)
add_test(NAME football_snapshot_history_test COMMAND football_snapshot_history_test)
set_tests_properties(football_snapshot_history_test PROPERTIES LABELS "sim;unit;snapshot")

add_executable(football_ball_allocation_test ../src/sim/ball/tests/ball_allocation_test.cpp)
target_link_libraries(football_ball_allocation_test PRIVATE football::ball Catch2::Catch2WithMain)
football_configure_target(football_ball_allocation_test)
add_test(NAME football_ball_allocation_test COMMAND football_ball_allocation_test)
set_tests_properties(football_ball_allocation_test PROPERTIES LABELS "ball;unit")

# Historical consumers also compile from values alone, without Simulation/AI/disk.
add_executable(football_snapshot_consumers_test snapshot_consumers_test.cpp
  ../src/sim/observation/snapshot_history.cpp ../src/sim/observation/temporal_graph.cpp
  ../src/sim/event/event_recognizer.cpp)
target_link_libraries(football_snapshot_consumers_test PRIVATE football::sim_contracts Catch2::Catch2WithMain)
football_configure_target(football_snapshot_consumers_test)
add_test(NAME football_snapshot_consumers_test COMMAND football_snapshot_consumers_test)
set_tests_properties(football_snapshot_consumers_test PROPERTIES LABELS "sim;unit;snapshot")

# The format/queue suite links recording only, proving it has no runtime actors.
add_executable(football_snapshot_archive_test snapshot_archive_test.cpp)
target_link_libraries(football_snapshot_archive_test PRIVATE football::recording Catch2::Catch2WithMain)
football_configure_target(football_snapshot_archive_test)
add_test(NAME football_snapshot_archive_test COMMAND football_snapshot_archive_test)
set_tests_properties(football_snapshot_archive_test PROPERTIES LABELS "app;unit;snapshot")

add_executable(football_snapshot_archive_integration_test snapshot_archive_integration_test.cpp)
target_link_libraries(football_snapshot_archive_integration_test PRIVATE
  football::recording football::game football::app_support Catch2::Catch2WithMain)
football_configure_target(football_snapshot_archive_integration_test)
add_test(NAME football_snapshot_archive_integration_test COMMAND football_snapshot_archive_integration_test)
set_tests_properties(football_snapshot_archive_integration_test PROPERTIES LABELS "app;integration;snapshot")
if(TARGET football_app)
  set(_snapshot_cli_artifact "${CMAKE_CURRENT_BINARY_DIR}/recorded-cli.snap")
  target_compile_definitions(football_snapshot_archive_integration_test PRIVATE
    FOOTBALL_TEST_CLI_SNAPSHOT_PATH="${_snapshot_cli_artifact}")
  add_test(NAME football_app_cli_recorded_match COMMAND football_app
    --half-duration-ms=1800 --snapshot-capacity=3 --snapshot=${_snapshot_cli_artifact})
  set_tests_properties(football_app_cli_recorded_match PROPERTIES
    FIXTURES_SETUP snapshot_cli_artifact LABELS "app;snapshot"
    PASS_REGULAR_EXPRESSION "home_score=0 away_score=0 outcome=draw duration_ticks=[1-9][0-9]*")
  add_test(NAME football_snapshot_archive_cli_readback
    COMMAND football_snapshot_archive_integration_test "[.archive-cli]")
  set_tests_properties(football_snapshot_archive_cli_readback PROPERTIES
    FIXTURES_REQUIRED snapshot_cli_artifact LABELS "app;snapshot")
endif()

# Pure period decisions read only explicit match/time values.
add_executable(football_period_test period_test.cpp ../src/sim/referee/period.cpp)
target_link_libraries(football_period_test PRIVATE
  football::sim_contracts Catch2::Catch2WithMain)
football_configure_target(football_period_test)
add_test(NAME football_period_test COMMAND football_period_test)
set_tests_properties(football_period_test PROPERTIES LABELS sim)

# This target proves that policy compiles/links without sim or libgame.
add_executable(football_default_ai_test default_ai_test.cpp)
target_link_libraries(football_default_ai_test PRIVATE
  football_ai Catch2::Catch2WithMain)
football_configure_target(football_default_ai_test)
add_test(NAME football_default_ai_test COMMAND football_default_ai_test)
set_tests_properties(football_default_ai_test PROPERTIES LABELS ai)

add_executable(football_sim_control_boundary_test
  sim_control_boundary_test.cpp sim_match_lifecycle_test.cpp restart_placement_test.cpp
  pitch_frame_test.cpp match_tick_test.cpp restart_readiness_test.cpp match_fatigue_test.cpp
  snapshot_capture_test.cpp
  actor_history_test.cpp custom_pitch_test.cpp referee_process_fixture.hpp)
target_link_libraries(football_sim_control_boundary_test PRIVATE
  football::game football::app_support Catch2::Catch2WithMain)
football_configure_target(football_sim_control_boundary_test)
add_test(NAME football_sim_control_boundary_test COMMAND football_sim_control_boundary_test)
set_tests_properties(football_sim_control_boundary_test PROPERTIES LABELS sim)

# C++ boundary/integration suites (formerly mixed into tools/).
add_executable(football_gameenv_test gameenv_test.cpp)
target_link_libraries(football_gameenv_test PRIVATE football::game football::app_support)
football_configure_target(football_gameenv_test)
add_test(NAME football_gameenv_test COMMAND football_gameenv_test)
set_tests_properties(football_gameenv_test PROPERTIES LABELS integration)

add_executable(football_player_identity_test player_identity_test.cpp)
target_link_libraries(football_player_identity_test PRIVATE football::game football::app_support)
football_configure_target(football_player_identity_test)
add_test(NAME football_player_identity_test COMMAND football_player_identity_test)

add_executable(football_sim_contracts_test sim_contracts_test.cpp)
target_link_libraries(football_sim_contracts_test PRIVATE football::sim_contracts)
football_configure_target(football_sim_contracts_test)
add_test(NAME football_sim_contracts_test COMMAND football_sim_contracts_test)

# Ball is an independent module: these tests link only football::ball (plus
# foundation/model geometry), never Simulation/Player/Team/Referee.
add_executable(football_ball_test
  ../src/sim/ball/tests/ball_state_test.cpp
  ../src/sim/ball/tests/ball_physics_test.cpp
  ../src/sim/ball/tests/ball_contact_test.cpp
  ../src/sim/ball/tests/ball_prediction_test.cpp
  ../src/sim/ball/tests/ball_tick_input_test.cpp
  ball_collision_test.cpp)
target_link_libraries(football_ball_test PRIVATE football::ball Catch2::Catch2WithMain)
football_configure_target(football_ball_test)
add_test(NAME football_ball_test COMMAND football_ball_test)
set_tests_properties(football_ball_test PROPERTIES LABELS "sim;ball")

add_executable(football_model_test model_test.cpp)
target_link_libraries(football_model_test PRIVATE football::model)
football_configure_target(football_model_test)
add_test(NAME football_model_test COMMAND football_model_test)
set_tests_properties(football_model_test football_player_identity_test football_sim_contracts_test
  PROPERTIES LABELS unit)

add_test(NAME football_regression COMMAND football_regression)
add_test(NAME football_animation_ab COMMAND football_regression --animation-ab)
add_test(NAME football_animation_ab_lifecycle COMMAND football_regression --animation-ab-lifecycle)
set_tests_properties(football_regression football_animation_ab football_animation_ab_lifecycle
  PROPERTIES LABELS regression ENVIRONMENT "GFOOTBALL_DATA_DIR=${PROJECT_SOURCE_DIR}/data")

foreach(reverse IN ITEMS 0 1)
  add_test(NAME football_restart_metrics_${reverse}
    COMMAND football_restart_metrics 42 500 ${reverse})
  set_tests_properties(football_restart_metrics_${reverse} PROPERTIES LABELS "sim;diagnostic")
  add_test(NAME football_restart_metrics_symmetric_${reverse}
    COMMAND football_restart_metrics 42 500 ${reverse} 1)
  set_tests_properties(football_restart_metrics_symmetric_${reverse}
    PROPERTIES LABELS "sim;diagnostic")
endforeach()

add_executable(football_anim_baking_test anim_baking_test.cpp)
target_link_libraries(football_anim_baking_test PRIVATE football_anim_baking Catch2::Catch2WithMain)
target_compile_definitions(football_anim_baking_test PRIVATE
  FOOTBALL_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/data")
football_configure_target(football_anim_baking_test)
add_test(NAME football_anim_baking_test COMMAND football_anim_baking_test)
set_tests_properties(football_anim_baking_test PROPERTIES LABELS animation TIMEOUT 180)

# Exercise actual CLI flags against a fresh artifact, without shell wrappers.
set(_baker_cli_artifact "${CMAKE_CURRENT_BINARY_DIR}/baker-cli.simanim")
add_test(NAME football_anim_baker_bake COMMAND football_anim_baker
  --input ${PROJECT_SOURCE_DIR}/data --out ${_baker_cli_artifact})
set_tests_properties(football_anim_baker_bake PROPERTIES
  FIXTURES_SETUP anim_baker_cli_artifact LABELS animation TIMEOUT 180)
foreach(operation IN ITEMS check verify verify-selection)
  add_test(NAME football_anim_baker_${operation} COMMAND football_anim_baker
    --input ${PROJECT_SOURCE_DIR}/data --${operation} ${_baker_cli_artifact})
  set_tests_properties(football_anim_baker_${operation} PROPERTIES
    FIXTURES_REQUIRED anim_baker_cli_artifact LABELS animation TIMEOUT 180)
endforeach()
add_test(NAME football_anim_baker_rejects_missing_source COMMAND football_anim_baker
  --input ${CMAKE_CURRENT_BINARY_DIR}/missing-import-source
  --out ${CMAKE_CURRENT_BINARY_DIR}/must-not-be-baked.simanim)
set_tests_properties(football_anim_baker_rejects_missing_source PROPERTIES
  WILL_FAIL TRUE LABELS animation)

add_executable(football_legacy_animation_import_test legacy_animation_import_test.cpp)
target_link_libraries(football_legacy_animation_import_test PRIVATE
  football_legacy_anim Catch2::Catch2WithMain)
football_configure_target(football_legacy_animation_import_test)
add_test(NAME football_legacy_animation_import_test COMMAND football_legacy_animation_import_test)
set_tests_properties(football_legacy_animation_import_test PROPERTIES LABELS animation)
