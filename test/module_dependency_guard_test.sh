#!/usr/bin/env sh
# Negative tests mutate only an isolated tree, never project sources.
set -eu
guard=$1
report=$2
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
mkdir -p "$work/src/sim" "$work/src/ai" "$work/src/app/input"
cp "$report" "$work/deps"
for header in world_state.hpp player_control.hpp player_control_set.hpp observation_epoch.hpp; do
  printf '#include "model/player.hpp"\n' > "$work/src/sim/$header"
done
printf '#include "sim/world_state.hpp"\n#include <sim/player_control_set.hpp>\n#include "sim/player_control.hpp"\n' > "$work/src/ai/policy.cpp"
printf '#include "sim/query/player_query.hpp"\n' > "$work/src/sim/query.cpp"
printf '#include "sim/world_state.hpp"\n' > "$work/src/app/input/input.cpp"
sh "$guard" "$work/src" "$work/deps"

reject() {
  if sh "$guard" "$work/src" "$work/deps" > "$work/output" 2>&1; then
    echo "guard accepted forbidden dependency: $1" >&2
    exit 1
  fi
  grep -q "$1" "$work/output"
}

printf '#include "ai/default_ai.hpp"\n' > "$work/src/sim/query.cpp"
reject 'src/sim includes ai/'
printf '# include <ai/default_ai.hpp>\n' > "$work/src/sim/query.cpp"
reject 'src/sim includes ai/'
printf '#include "sim/query/player_query.hpp"\n' > "$work/src/sim/query.cpp"
sed 's/^football_sim: .*/& football_ai/' "$report" > "$work/deps"
reject 'forbidden football_sim -> football_ai link'
cp "$report" "$work/deps"

# Same directory does not mean same contract. Check runtime headers/functions,
# both quoting styles, and a transitive leak through a contract header.
for header in sim/match.hpp sim/player/player.hpp sim/formation.hpp sim/simulation.hpp sim/animation/types.hpp; do
  for style in quoted angle; do
    if [ "$style" = quoted ]; then
      printf '#include "%s"\n' "$header" > "$work/src/ai/policy.cpp"
    else
      printf '# include <%s>\n' "$header" > "$work/src/ai/policy.cpp"
    fi
    reject 'src/ai includes sim/'
  done
done
printf '#include "sim/world_state.hpp"\n' > "$work/src/ai/policy.cpp"
printf '#include "sim/match.hpp"\n' > "$work/src/sim/world_state.hpp"
reject 'sim contract sim/world_state.hpp includes sim/match.hpp'
printf '#include "match.hpp"\n' > "$work/src/sim/world_state.hpp"
reject 'sim contract sim/world_state.hpp includes match.hpp'
for header in ../sim/match.hpp model/../sim/match.hpp model/./player.hpp; do
  printf '#include "%s"\n' "$header" > "$work/src/ai/policy.cpp"
  reject "src/ai non-canonical include $header"
done
printf '#include "sim/world_state.hpp"\n' > "$work/src/ai/policy.cpp"
printf '#include "model/player.hpp"\n' > "$work/src/sim/world_state.hpp"
printf '#include "sim/../ai/policy.hpp"\n' > "$work/src/sim/query.cpp"
reject 'src/sim non-canonical include sim/../ai/policy.hpp'
printf '#include "sim/query/player_query.hpp"\n' > "$work/src/sim/query.cpp"

for target in football_sim football_animation football_engine football_controller football_support football_app_support football_app_input; do
  sed "s/^football_ai: .*/& $target/" "$report" > "$work/deps"
  reject "forbidden football_ai -> $target link"
  sed "s/^football_sim_contracts: .*/& $target/" "$report" > "$work/deps"
  reject "forbidden football_sim_contracts -> $target link"
done
sed 's/^football_sim_contracts: .*/& football_ai/' "$report" > "$work/deps"
reject 'forbidden football_sim_contracts -> football_ai link'
cp "$report" "$work/deps"
sed 's/^football_ai: .*/& football_actor_alias/' "$report" > "$work/deps"
reject 'forbidden football_ai -> football_actor_alias link'
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
printf 'TacticalBoard ObserveTactics();\n' > "$work/src/sim/query.cpp"
reject 'tactical intent leaked into simulation contracts'
printf '#include "sim/query/player_query.hpp"\n' > "$work/src/sim/query.cpp"
printf 'struct PlayerDirective {};\n' > "$work/src/sim/world_state.hpp"
reject 'tactical intent leaked into simulation contracts'
printf '#include "model/player.hpp"\n' > "$work/src/sim/world_state.hpp"
for retired in observation control controller data sim/player/controller; do
  mkdir -p "$work/src/$retired"
  reject "retired src/$retired directory"
  rmdir "$work/src/$retired"
done
printf '#include "observation/world_state.hpp"\n' > "$work/src/ai/policy.cpp"
reject 'src/ai includes observation/'
printf '#include "sim/world_state.hpp"\n' > "$work/src/ai/policy.cpp"
for target in football_ai football_sim football_animation football_engine football_support football_app_support; do
  sed "s/^football_app_input: .*/& $target/" "$report" > "$work/deps"
  reject "forbidden football_app_input -> $target link"
done
cp "$report" "$work/deps"
printf '#include "sim/match.hpp"\n' > "$work/src/app/input/input.cpp"
reject 'src/app/input includes sim/match.hpp'
printf '#include "sim/world_state.hpp"\n' > "$work/src/app/input/input.cpp"
for header in ai/default_ai.hpp ai/tactical_board.hpp ai/team_requests.hpp; do
  printf '#include "%s"\n' "$header" > "$work/src/app/input/input.cpp"
  reject "src/app/input includes $header"
done
printf '#include "sim/world_state.hpp"\n' > "$work/src/app/input/input.cpp"
printf 'class DefaultAI;\n' > "$work/src/app/input/input.cpp"
reject 'concrete policy in value-only input'
printf '#include "sim/world_state.hpp"\n' > "$work/src/app/input/input.cpp"
mkdir -p "$work/src/env"
printf 'DefaultAI& default_ai();\n' > "$work/src/env/game_env.hpp"
reject 'concrete policy exposed in env public API'
printf '#include "ai/default_ai.hpp"\n' > "$work/src/env/game_env.hpp"
reject 'concrete policy exposed in env public API'
rm "$work/src/env/game_env.hpp"
for symbol in HumanController HumanGamer PlayerController ControllerInput ExternalController TeamTacticalState attacking_run_remaining_ms externally_controlled; do
  printf 'struct %s {};\n' "$symbol" > "$work/src/sim/query.cpp"
  reject 'retired input/tactical authority in core'
done
printf '#include "sim/query/player_query.hpp"\n' > "$work/src/sim/query.cpp"
mkdir "$work/src/new_strategy_module"
reject 'unsupported top-level module new_strategy_module'
rmdir "$work/src/new_strategy_module"
for core in football_sim football_foundation football_engine; do
  sed "s/^$core: .*/& football_app_input/" "$report" > "$work/deps"
  reject "forbidden $core -> football_app_input link"
done
cp "$report" "$work/deps"
touch "$work/src/sim/team_tactical_state.hpp"
reject 'retired sim/team_tactical_state.hpp file'
rm "$work/src/sim/team_tactical_state.hpp"
sh "$guard" "$work/src" "$work/deps"
