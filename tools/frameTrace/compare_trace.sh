#!/usr/bin/env bash
# MIR-0 A/B comparator for the normalized frame trace.
#
# Usage:
#   compare_trace.sh <baseline.trace> <changed.trace>
#
# The trace is one record per line, ordered by tick then stable entity id
# (player id, event sequence), so the first differing line names the tick,
# entity and field where the two builds diverge. Exit code 0 means identical,
# 1 means a divergence was found.
set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "usage: $0 <baseline.trace> <changed.trace>" >&2
  exit 2
fi

baseline=$1
changed=$2

if diff -q "$baseline" "$changed" >/dev/null; then
  echo "IDENTICAL: $baseline == $changed"
  exit 0
fi

echo "FIRST DIVERGENCE ($baseline -> $changed):"
# -u keeps the report stable; the first hunk is the first divergent tick/field.
diff -u "$baseline" "$changed" | sed -n '1,40p'
exit 1
