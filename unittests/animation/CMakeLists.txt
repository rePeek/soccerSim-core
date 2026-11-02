# animation — the shared, immutable clip resource and its pure algorithms: pose
# sampling/blending, entry frames, transitions, import quality and pure geometry.
# Candidate/action selection belongs to the Player (unittests/player,
# unittests/player_decision).

add_executable(football_animation_unit_tests
  catalog_loader_test.cpp
  motion_clip_test.cpp
  root_sampling_contract_test.cpp
  pose_sampling_test.cpp
  pose_blend_test.cpp
  entry_frame_continuity_test.cpp
  touch_geometry_test.cpp
  foot_support_test.cpp
  motion_quality_test.cpp
  reference_sample_test.cpp)
football_require_animation_cache(football_animation_unit_tests)
# Import verification may inspect support workspaces; production consumers cannot.
target_include_directories(football_animation_unit_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/src/animation/import)
target_link_libraries(football_animation_unit_tests
  PRIVATE
    football::animation
    nlohmann_json::nlohmann_json
    Catch2::Catch2WithMain)
football_configure_target(football_animation_unit_tests)

add_test(NAME animation_unit COMMAND football_animation_unit_tests)
set_tests_properties(animation_unit PROPERTIES
  LABELS "unit;animation"
  TIMEOUT 60)

# Whole archives prove the import and runtime link closure without Player/world.
add_executable(football_animation_link_smoke animation_link_smoke.cpp)
target_link_libraries(football_animation_link_smoke PRIVATE
  "$<LINK_LIBRARY:WHOLE_ARCHIVE,football_animation>"
  "$<LINK_LIBRARY:WHOLE_ARCHIVE,football_animation_base>")
football_configure_target(football_animation_link_smoke)
add_test(NAME animation_link_smoke COMMAND football_animation_link_smoke)
set_tests_properties(animation_link_smoke PROPERTIES
  LABELS "interface;animation" TIMEOUT 10)

# Every exported header must compile on its own using exported include paths.
set(_animation_header_smoke_sources)
foreach(_target football_animation_base football_animation)
  get_target_property(_headers ${_target} HEADER_SET)
  foreach(_header IN LISTS _headers)
    get_filename_component(_name "${_header}" NAME_WE)
    file(RELATIVE_PATH _include "${PROJECT_SOURCE_DIR}/include" "${_header}")
    set(_source "${CMAKE_CURRENT_BINARY_DIR}/${_name}_header.cpp")
    file(GENERATE OUTPUT "${_source}" CONTENT "#include \"${_include}\"\n")
    list(APPEND _animation_header_smoke_sources "${_source}")
  endforeach()
endforeach()
add_library(football_animation_header_smoke OBJECT ${_animation_header_smoke_sources})
target_link_libraries(football_animation_header_smoke PRIVATE football::animation)
football_configure_target(football_animation_header_smoke)
