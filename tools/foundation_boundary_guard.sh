#!/usr/bin/env sh
# Foundation is the bottom of the source dependency DAG.
#
# Two things are checked:
#   1. no include of an upper layer (env/sim/data/animation).
#   2. no reference to context/state authority, which would make foundation
#      depend on a caller instead of the other way around. Random number
#      *algorithms* are allowed here (foundation/math/rng.hpp); the global
#      RNG entry points that reach into the game context are not.
set -eu

if [ "$#" -ne 1 ]; then
  echo "usage: $0 SOURCE_DIR" >&2
  exit 2
fi

source_dir=$1
forbidden_include='^[[:space:]]*#[[:space:]]*include[[:space:]]*[<"][^">]*(env|sim|data|animation)/[^">]*[">]'
forbidden_authority='\b(GetContext|EnvState|boostrandom|randomseed|random_non_determ)\b'

status=0

if grep -R -n -E --include='*.cpp' --include='*.hpp' --include='*.h' \
    "$forbidden_include" "$source_dir/foundation"; then
  echo "foundation boundary guard: foundation may not include an upper layer" >&2
  status=1
fi

if grep -R -n -E --include='*.cpp' --include='*.hpp' --include='*.h' \
    "$forbidden_authority" "$source_dir/foundation"; then
  echo "foundation boundary guard: foundation may not reach into game context" >&2
  status=1
fi

exit "$status"
