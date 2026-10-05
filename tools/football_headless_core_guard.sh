#!/usr/bin/env sh
# Keep the headless-core boundary mechanical: the shared object may not
# acquire a graphics dependency, and core source may not include the retired
# engine.
set -eu

if [ "$#" -ne 2 ]; then
  echo "usage: $0 LIBGAME SOURCE_DIR" >&2
  exit 2
fi

library=$1
source_dir=$2
forbidden_needed='Shared library: \[(lib(SDL2|EGL|GLX|OpenGL|GL\.so|X11)[^]]*)\]'
forbidden_include='^[[:space:]]*#[[:space:]]*include[[:space:]]*[<"][^">]*(SDL|EGL|GLX|OpenGL|GL/|systems/graphics|scene/scene2d|scene/scene3d|utils/gui2|menu/)[^">]*[">]'

if readelf -d "$library" | grep -E "$forbidden_needed"; then
  echo "headless-core guard: forbidden NEEDED entry in $library" >&2
  exit 1
fi

if grep -R -n -E \
    --include='*.cpp' --include='*.hpp' --include='*.h' \
    "$forbidden_include" "$source_dir"; then
  echo "headless-core guard: retired engine include in $source_dir" >&2
  exit 1
fi

# Rules must not regain on-pitch referee/linesman actors or animation-driven
# restart timing. Referee itself is the football rules engine and stays.
# Player is concrete: do not restore the retired shared player/official base.
if grep -R -n -E \
    --include='*.cpp' --include='*.hpp' --include='*.h' \
    '\b(Officials|PlayerOfficial|RefereeController|GetOfficials|GetOfficialPlayers|AlterSetPiecePrepareTime|PlayerBase)\b|playerbase\.(hpp|cpp)' \
    "$source_dir"; then
  echo "headless-core guard: retired actor abstraction or timing hook in $source_dir" >&2
  exit 1
fi
