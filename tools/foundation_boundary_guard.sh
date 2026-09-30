#!/usr/bin/env sh
# Foundation is the bottom of the source dependency DAG.
set -eu

if [ "$#" -ne 1 ]; then
  echo "usage: $0 SOURCE_DIR" >&2
  exit 2
fi

source_dir=$1
forbidden_include='^[[:space:]]*#[[:space:]]*include[[:space:]]*[<"][^">]*(env|sim|data|animation)/[^">]*[">]'

if grep -R -n -E --include='*.cpp' --include='*.hpp' --include='*.h' \
    "$forbidden_include" "$source_dir/foundation"; then
  echo "foundation boundary guard: foundation may not include an upper layer" >&2
  exit 1
fi
