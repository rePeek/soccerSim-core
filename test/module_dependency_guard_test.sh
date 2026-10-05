#!/usr/bin/env sh
# Negative tests for ownership enforcement; never mutate the real source tree.
set -eu
guard=$1
report=$2
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
mkdir -p "$work/src/sim" "$work/src/ai"
cp "$report" "$work/deps"
printf '#include "sim/match.hpp"\n' > "$work/src/ai/policy.cpp"
printf '#include "sim/query/player_query.hpp"\n' > "$work/src/sim/query.cpp"
sh "$guard" "$work/src" "$work/deps"

reject() {
  expected=$1
  if sh "$guard" "$work/src" "$work/deps" > "$work/output" 2>&1; then
    echo "guard accepted forbidden dependency: $expected" >&2
    exit 1
  fi
  grep -q "$expected" "$work/output"
}

# ai/ used to be omitted from the include regex even after adding its provider.
printf '#include "ai/eliza_controller.hpp"\n' > "$work/src/sim/query.cpp"
reject 'src/sim includes ai/'
printf '# include <ai/eliza_controller.hpp>\n' > "$work/src/sim/query.cpp"
reject 'src/sim includes ai/'
printf '#include "sim/query/player_query.hpp"\n' > "$work/src/sim/query.cpp"

# Declaring even an indirect reverse edge must not legalize it.
sed 's/^football_sim: .*/& football_ai/' "$report" > "$work/deps"
reject 'forbidden football_sim -> football_ai link'
cp "$report" "$work/deps"

printf 'void AI_GetClosestPlayer();\n' > "$work/src/sim/query.cpp"
reject 'obsolete AIfunctions/AI_ API in sim'
printf '#include "sim/ai_support/AIfunctions.hpp"\n' > "$work/src/sim/query.cpp"
reject 'obsolete AIfunctions/AI_ API in sim'
printf '#include "sim/query/player_query.hpp"\n' > "$work/src/sim/query.cpp"
mkdir -p "$work/src/sim/ai_support"
touch "$work/src/sim/ai_support/AIfunctions.hpp"
reject 'obsolete AIfunctions/AI_ API in sim'
rm "$work/src/sim/ai_support/AIfunctions.hpp"
sh "$guard" "$work/src" "$work/deps"
