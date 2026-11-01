# Header-only parser; its implementation is compiled privately by Animation.
CPMAddPackage(NAME cgltf
  GITHUB_REPOSITORY jkuhlmann/cgltf
  GIT_TAG bbeb5b0b070ddacddac6852fb72143eb68454937 # v1.15
  DOWNLOAD_ONLY YES)
add_library(football_cgltf INTERFACE)
target_include_directories(football_cgltf SYSTEM INTERFACE "${cgltf_SOURCE_DIR}")
