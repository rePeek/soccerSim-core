#!/usr/bin/env sh
# Keep the animation baker standalone: it drives the legacy AnimCollection
# loader directly and must not reference the runtime environment (GameEnv /
# Match / global GameContext accessors) or pull env headers back in.
set -eu

if [ "$#" -ne 1 ]; then
  echo "usage: $0 BAKER_SOURCE" >&2
  exit 2
fi

source=$1

forbidden='GameEnv|GameContext|GetContext|GetGameConfig|\bMatch\b|env/game_env|env/main'
if grep -nE "$forbidden" "$source"; then
  echo "anim_baker guard: standalone baker must not reference the runtime env" >&2
  exit 1
fi
