#!/usr/bin/env sh
# Bake determinism guard: the same source + same baker must produce a
# byte-identical simanim, and the written artifact must round-trip against a
# fresh bake via --check.
set -eu

if [ "$#" -ne 2 ]; then
  echo "usage: $0 FOOTBALL_ANIM_BAKER DATA_DIR" >&2
  exit 2
fi

baker=$1
data_dir=$2
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

"$baker" --input "$data_dir" --out "$tmp/a.simanim"
"$baker" --input "$data_dir" --out "$tmp/b.simanim"
cmp "$tmp/a.simanim" "$tmp/b.simanim"
"$baker" --input "$data_dir" --check "$tmp/a.simanim"
"$baker" --input "$data_dir" --verify "$tmp/a.simanim"
"$baker" --input "$data_dir" --verify-selection "$tmp/a.simanim"
