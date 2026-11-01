# Runtime reads canonical GLBs and catalog metadata directly. No Python/baker
# invocation, generated animation cache, or asset rebuild is required.
set(FOOTBALL_ANIMATION_MANIFEST
    "${PROJECT_SOURCE_DIR}/assets/animations/manifest.json")

function(football_require_animation_catalog target)
  target_compile_definitions(${target} PRIVATE
    GFOOTBALL_ANIMATION_CATALOG_PATH="${FOOTBALL_ANIMATION_MANIFEST}")
endfunction()

# Compatibility for existing test fixtures. Despite the historic name these
# now load the source catalog, not a generated cache.
function(football_require_animation_cache target)
  football_require_animation_catalog(${target})
  target_compile_definitions(${target} PRIVATE
    GFOOTBALL_BAKED_ANIM_PATH="${FOOTBALL_ANIMATION_MANIFEST}")
endfunction()
