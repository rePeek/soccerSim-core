# GLB + football metadata are the sources. SIMANIM is a generated, private
# runtime cache; neither Blender nor the legacy .anim baker is needed to build.
find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
set(FOOTBALL_ANIMATION_MANIFEST
    "${PROJECT_SOURCE_DIR}/assets/animations/player_14/manifest.json")
set(FOOTBALL_ANIMATION_CACHE
    "${PROJECT_BINARY_DIR}/assets/runtime/animations.simanim")
# Use the manifest, not a glob: deleting a referenced GLB must fail even if
# a previous cache exists. Catalog edits also refresh the dependency list.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${FOOTBALL_ANIMATION_MANIFEST}")
execute_process(
  COMMAND "${Python3_EXECUTABLE}" -c
    "import json,pathlib,sys; p=pathlib.Path(sys.argv[1]); print(';'.join(str(p.parent / c['file']) for c in json.loads(p.read_text())['clips']))"
    "${FOOTBALL_ANIMATION_MANIFEST}"
  COMMAND_ERROR_IS_FATAL ANY
  OUTPUT_VARIABLE FOOTBALL_ANIMATION_GLBS
  OUTPUT_STRIP_TRAILING_WHITESPACE)
add_custom_command(
  OUTPUT "${FOOTBALL_ANIMATION_CACHE}"
  COMMAND "${Python3_EXECUTABLE}"
    "${PROJECT_SOURCE_DIR}/tools/playerAssets/animation_assets.py" bake
    --manifest "${FOOTBALL_ANIMATION_MANIFEST}"
    --output "${FOOTBALL_ANIMATION_CACHE}"
  DEPENDS ${FOOTBALL_ANIMATION_GLBS}
    "${FOOTBALL_ANIMATION_MANIFEST}"
    "${PROJECT_SOURCE_DIR}/assets/skeletons/player_14.json"
    "${PROJECT_SOURCE_DIR}/tools/playerAssets/animation_assets.py"
    "${PROJECT_SOURCE_DIR}/tools/playerAssets/simanim.py"
    "${PROJECT_SOURCE_DIR}/tools/playerAssets/legacy.py"
  COMMENT "Compiling GLB animation catalog into runtime cache"
  VERBATIM)
add_custom_target(football_animation_assets ALL
  DEPENDS "${FOOTBALL_ANIMATION_CACHE}")

function(football_require_animation_cache target)
  add_dependencies(${target} football_animation_assets)
  target_compile_definitions(${target} PRIVATE
    GFOOTBALL_BAKED_ANIM_PATH="${FOOTBALL_ANIMATION_CACHE}")
endfunction()

if(BUILD_TESTING)
  add_test(NAME animation_asset_tools
    COMMAND "${Python3_EXECUTABLE}" -m unittest discover
      -s "${PROJECT_SOURCE_DIR}/tools/playerAssets" -v)
  set_tests_properties(animation_asset_tools PROPERTIES
    LABELS "unit;animation" TIMEOUT 60)
endif()
