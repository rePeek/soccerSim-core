#!/usr/bin/env sh
# Prevent simulation runtime code from regaining a source-animation dependency.
set -eu

if [ "$#" -ne 1 ]; then
  echo "usage: $0 SOURCE_DIR" >&2
  exit 2
fi

source_dir=$1
forbidden_headers='
  animation/animcollection.hpp
  animation/animation.hpp
  animation/import_loader.hpp
  animation/import_hierarchy.hpp
  animation/xmlloader.hpp
'

for header in $forbidden_headers; do
  if grep -R -n -F --include='*.cpp' --include='*.hpp' --include='*.h' \
      "#include \"$header\"" "$source_dir/sim" "$source_dir/env"; then
    echo "runtime animation boundary guard: forbidden include $header" >&2
    exit 1
  fi
done
