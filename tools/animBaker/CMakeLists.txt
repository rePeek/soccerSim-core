# Offline .anim/XML pipeline, never a runtime/shared-library dependency.
add_library(football_legacy_anim OBJECT
  animation/animation.cpp
  animation/animcollection.cpp
  animation/import_hierarchy.cpp
  animation/import_loader.cpp
  animation/extensions/footballanimationextension.cpp
  import/legacy_xml.cpp import/legacy_xml.hpp
  import/legacy_value_codec.cpp import/legacy_value_codec.hpp
  import/legacy_text.cpp import/legacy_text.hpp)
target_link_libraries(football_legacy_anim PUBLIC football::foundation)
target_include_directories(football_legacy_anim PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
football_configure_target(football_legacy_anim)

add_library(football_anim_baking STATIC anim_baker.cpp)
target_sources(football_anim_baking PUBLIC FILE_SET HEADERS
  BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
  FILES anim_baker.hpp)
target_link_libraries(football_anim_baking
  PUBLIC football::animation
  PRIVATE football_legacy_anim football::foundation)
football_configure_target(football_anim_baking)

add_executable(football_anim_baker anim_baker_main.cpp)
target_link_libraries(football_anim_baker PRIVATE football_anim_baking)
football_configure_target(football_anim_baker)
