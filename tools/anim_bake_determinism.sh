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

GFOOTBALL_DATA_DIR="$data_dir" "$baker" --out "$tmp/a.simanim"
GFOOTBALL_DATA_DIR="$data_dir" "$baker" --out "$tmp/b.simanim"
cmp "$tmp/a.simanim" "$tmp/b.simanim"
GFOOTBALL_DATA_DIR="$data_dir" "$baker" --check "$tmp/a.simanim"
