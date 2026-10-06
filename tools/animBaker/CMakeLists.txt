# Offline .anim/XML pipeline, never a runtime/shared-library dependency.
add_library(football_legacy_anim OBJECT
  animation/animation.cpp
  animation/animcollection.cpp
  animation/import_hierarchy.cpp
  animation/import_loader.cpp
  animation/extensions/footballanimationextension.cpp)
target_link_libraries(football_legacy_anim PUBLIC football::support football::foundation)
target_include_directories(football_legacy_anim PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
football_configure_target(football_legacy_anim)

add_executable(football_anim_baker anim_baker_main.cpp)
target_link_libraries(football_anim_baker PRIVATE
  football_legacy_anim football::animation football::foundation football::support)
football_configure_target(football_anim_baker)
add_test(NAME football_anim_baker_standalone_guard
  COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/anim_baker_standalone_guard.sh
    ${CMAKE_CURRENT_SOURCE_DIR}/anim_baker_main.cpp)
add_test(NAME football_anim_bake_determinism
  COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/anim_bake_determinism.sh
    $<TARGET_FILE:football_anim_baker> ${PROJECT_SOURCE_DIR}/data)
