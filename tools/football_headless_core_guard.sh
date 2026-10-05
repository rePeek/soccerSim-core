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
# Model identity must not fall back to the old ambient allocation ordinal.
if grep -R -n -E \
    --include='*.cpp' --include='*.hpp' --include='*.h' \
    '\b(Officials|PlayerOfficial|RefereeController|GetOfficials|GetOfficialPlayers|AlterSetPiecePrepareTime|PlayerBase|stablePlayerCount|GetStableID)\b|playerbase\.(hpp|cpp)|model/ids\.hpp' \
    "$source_dir"; then
  echo "headless-core guard: retired abstraction, numbering or timing hook in $source_dir" >&2
  exit 1
fi

# GameEnv owns Simulation directly. Retired binding/lifecycle APIs are forbidden
# everywhere, including internal diagnostic tools and shared-library symbols.
retired_context='\b(GameContext|GetContext|GetGame|SetGame|ContextHolder|run_game|quit_game|e_RenderingMode|GameState)\b|env/main\.(hpp|cpp)'
if grep -R -n -E \
    --include='*.cpp' --include='*.hpp' --include='*.h' \
    "$retired_context" "$source_dir" \
    "$source_dir/../tools/football_regression.cpp" \
    "$source_dir/../tools/player_identity_test.cpp"; then
  echo "headless-core guard: retired environment binding in runtime/diagnostics" >&2
  exit 1
fi
if nm -C "$library" | grep -E "$retired_context"; then
  echo "headless-core guard: retired environment binding in $library" >&2
  exit 1
fi
if [ -e "$source_dir/env/main.hpp" ] || [ -e "$source_dir/env/main.cpp" ]; then
  echo "headless-core guard: retired environment binding file" >&2
  exit 1
fi

# Simulation/data cannot grow a dependency on the public environment either.
if grep -R -n -E \
    --include='*.cpp' --include='*.hpp' --include='*.h' \
    '^[[:space:]]*#[[:space:]]*include[[:space:]]*[<"]env/' \
    "$source_dir/sim" "$source_dir/data"; then
  echo "headless-core guard: environment dependency in simulation/data" >&2
  exit 1
fi
