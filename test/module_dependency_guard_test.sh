#!/usr/bin/env sh
# Negative tests for ownership enforcement; never mutate the real source tree.
set -eu
guard=$1
report=$2
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
mkdir -p "$work/src/sim" "$work/src/ai"
cp "$report" "$work/deps"
printf '#include "observation/world_state.hpp"\n' > "$work/src/ai/policy.cpp"
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
printf '#include "ai/default_ai.hpp"\n' > "$work/src/sim/query.cpp"
reject 'src/sim includes ai/'
printf '# include <ai/default_ai.hpp>\n' > "$work/src/sim/query.cpp"
reject 'src/sim includes ai/'
printf '#include "sim/query/player_query.hpp"\n' > "$work/src/sim/query.cpp"

# Declaring even an indirect reverse edge must not legalize it.
sed 's/^football_sim: .*/& football_ai/' "$report" > "$work/deps"
reject 'forbidden football_sim -> football_ai link'
cp "$report" "$work/deps"

printf '#include "sim/match.hpp"\n' > "$work/src/ai/policy.cpp"
reject 'src/ai includes sim/'
printf '# include <sim/player/player.hpp>\n' > "$work/src/ai/policy.cpp"
reject 'src/ai includes sim/'
printf '#include "observation/world_state.hpp"\n' > "$work/src/ai/policy.cpp"
for target in football_sim football_animation football_engine football_controller football_support football_app_support; do
  sed "s/^football_ai: .*/& $target/" "$report" > "$work/deps"
  reject "forbidden football_ai -> $target link"
done
cp "$report" "$work/deps"

printf 'class LegacyPlayerDecision;\n' > "$work/src/sim/query.cpp"
reject 'retired decision object API'
printf '#include "sim/query/player_query.hpp"\n' > "$work/src/sim/query.cpp"
touch "$work/src/sim/legacy_decision_factories.hpp"
reject 'retired decision object file'
rm "$work/src/sim/legacy_decision_factories.hpp"

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
