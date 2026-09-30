#!/usr/bin/env sh
# Controller input protocol is independent of lifecycle and simulation
# authority. Concrete legacy adapters may live above this module while the
# controller target itself stays reusable by any frontend.
set -eu

if [ "$#" -ne 1 ]; then
  echo "usage: $0 SOURCE_DIR" >&2
  exit 2
fi

source_dir=$1
forbidden_include='^[[:space:]]*#[[:space:]]*include[[:space:]]*[<"][^">]*(env|sim|data|animation|support)/[^">]*[">]'
forbidden_authority='\b(GetContext|EnvState|GameEnv|Log|print_stacktrace)\b'
status=0

if grep -R -n -E --include='*.cpp' --include='*.hpp' --include='*.h' \
    "$forbidden_include" "$source_dir/controller"; then
  echo "controller boundary guard: controller may only depend on foundation" >&2
  status=1
fi

if grep -R -n -E --include='*.cpp' --include='*.hpp' --include='*.h' \
    "$forbidden_authority" "$source_dir/controller"; then
  echo "controller boundary guard: controller may not own environment or simulation authority" >&2
  status=1
fi

exit "$status"
