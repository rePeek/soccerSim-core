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
    TIMEOUT 180)
  add_test(NAME football_app_cli_short_match COMMAND football_app --half-duration-ms=1800)
  set_tests_properties(football_app_cli_short_match PROPERTIES
    PASS_REGULAR_EXPRESSION "home_score=0 away_score=0 outcome=draw duration_ticks=[1-9][0-9]*")
  foreach(value IN ITEMS abc -5 0 18446744073709551615)
    add_test(NAME football_app_cli_rejects_duration_${value}
      COMMAND football_app --half-duration-ms=${value})
    set_tests_properties(football_app_cli_rejects_duration_${value} PROPERTIES WILL_FAIL TRUE)
  endforeach()
  add_test(NAME football_app_cli_rejects_tick_budget COMMAND football_app --steps=10)
  set_tests_properties(football_app_cli_rejects_tick_budget PROPERTIES WILL_FAIL TRUE)
endif()


# Runtime computation ownership has its own focused tests. App descriptions
# remain test inputs and never become a runtime dependency.
add_executable(football_sim_computation_test sim_computation_test.cpp tick_test.cpp)
target_link_libraries(football_sim_computation_test PRIVATE
  football::game football::app_support Catch2::Catch2WithMain)
football_configure_target(football_sim_computation_test)
add_test(NAME football_sim_computation_test COMMAND football_sim_computation_test)
set_tests_properties(football_sim_computation_test PROPERTIES LABELS sim)

# This target proves that policy compiles/links without sim or libgame.
add_executable(football_default_ai_test default_ai_test.cpp)
target_link_libraries(football_default_ai_test PRIVATE
  football_ai Catch2::Catch2WithMain)
football_configure_target(football_default_ai_test)
add_test(NAME football_default_ai_test COMMAND football_default_ai_test)
set_tests_properties(football_default_ai_test PROPERTIES LABELS ai)

add_executable(football_sim_control_boundary_test
  sim_control_boundary_test.cpp sim_match_lifecycle_test.cpp restart_placement_test.cpp
  pitch_frame_test.cpp)
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
