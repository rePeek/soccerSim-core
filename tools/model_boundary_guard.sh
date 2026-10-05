#!/usr/bin/env sh
# Static domain descriptions may depend only on STL and other model headers.
set -eu

if [ "$#" -ne 1 ]; then
  echo "usage: $0 SOURCE_DIR" >&2
  exit 2
fi

source_dir=$1
forbidden_include='^[[:space:]]*#[[:space:]]*include[[:space:]]*[<"][^">]*(foundation|env|sim|data|animation|control|controller|observation|support|domain)/[^">]*[">]'
forbidden_authority='\b(GetContext|GetGame|EnvState|boostrandom|randomseed|random_non_determ|Log)\b'
forbidden_infrastructure='\b(fstream|filesystem|ifstream|ofstream|XMLLoader|XMLTree)\b'
forbidden_runtime_detail='\b(PlayerIndex|schedule_phase_|schedule_phase)\b'
status=0

if grep -R -n -E --include='*.cpp' --include='*.hpp' --include='*.h' \
    "$forbidden_include" "$source_dir/model"; then
  echo "model boundary guard: model may not include another project layer" >&2
  status=1
fi

if grep -R -n -E --include='*.cpp' --include='*.hpp' --include='*.h' \
    "$forbidden_authority|$forbidden_infrastructure|$forbidden_runtime_detail" "$source_dir/model"; then
  echo "model boundary guard: model may not access runtime scheduling/state or I/O" >&2
  status=1
fi

exit "$status"
