#!/usr/bin/env sh
# Foundation is pure value types and algorithms. It may not acquire upper-layer
# authority or side-effect/infrastructure dependencies.
set -eu

if [ "$#" -ne 1 ]; then
  echo "usage: $0 SOURCE_DIR" >&2
  exit 2
fi

source_dir=$1
forbidden_include='^[[:space:]]*#[[:space:]]*include[[:space:]]*[<"][^">]*(env|sim|data|animation|support)/[^">]*[">]'
forbidden_authority='\b(GetContext|EnvState|boostrandom|randomseed|random_non_determ|Log|print_stacktrace|install_stacktrace|DO_VALIDATION)\b'
forbidden_infrastructure='\b(fstream|filesystem|XML|ifstream|ofstream)\b'

status=0

if grep -R -n -E --include='*.cpp' --include='*.hpp' --include='*.h' \
    "$forbidden_include" "$source_dir/foundation"; then
  echo "foundation boundary guard: foundation may not include an upper layer or support" >&2
  status=1
fi

if grep -R -n -E --include='*.cpp' --include='*.hpp' --include='*.h' \
    "$forbidden_authority" "$source_dir/foundation"; then
  echo "foundation boundary guard: foundation may not reach into authority or diagnostics" >&2
  status=1
fi

if grep -R -n -E --include='*.cpp' --include='*.hpp' --include='*.h' \
    "$forbidden_infrastructure" "$source_dir/foundation"; then
  echo "foundation boundary guard: foundation may not contain I/O or external representations" >&2
  status=1
fi

exit "$status"
