#!/usr/bin/env sh
# The old per-statement validation hook was deleted. Keep its macro/function
# names out of C++ sources so the instrumentation layer cannot grow back.
set -eu

if [ "$#" -ne 1 ]; then
  echo "usage: $0 PROJECT_ROOT" >&2
  exit 2
fi

project_root=$1
forbidden='\b(DO_VALIDATION|DoValidation|FULL_VALIDATION)\b'

if grep -R -n -E --include='*.cpp' --include='*.hpp' --include='*.h' \
    "$forbidden" "$project_root/src" "$project_root/tools"; then
  echo "legacy validation guard: deleted validation hook found" >&2
  exit 1
fi
