# Bounded runtime regression harness. Links the complete simulation composition
# (no AI/app/simcore) and never runs a full match; the wall-clock timeout guards
# a hung Step. These cases are the match-level gate while Runtime is decomposed:
# deterministic TickTrace replay, physical invariants, period/change-of-ends and
# the ruling path. Set FOOTBALL_TRACE_PATH when running the trace case to export
# a per-tick file for a manual before/after diff.
add_executable(football_sim_runtime_tests
  runtime_trace_test.cpp
  referee_ruling_test.cpp
  period_change_test.cpp
  match_physics_test.cpp)
target_link_libraries(football_sim_runtime_tests
  PRIVATE football::sim Catch2::Catch2WithMain)
# the harness includes the internal headers through the shared src/ root.
target_include_directories(football_sim_runtime_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
football_configure_target(football_sim_runtime_tests)
football_require_animation_catalog(football_sim_runtime_tests)

add_test(NAME sim_runtime COMMAND football_sim_runtime_tests)
set_tests_properties(sim_runtime PROPERTIES
  LABELS "interface;runtime;sim"
  TIMEOUT 600)
