#!/usr/bin/env sh
# Enforce real CMake link closures, with exact header membership for sim's
# value-only contracts. Sharing a sim/ directory must not expose actor runtime.
set -eu
if [ "$#" -ne 2 ]; then
  echo "usage: $0 SOURCE_DIR MODULE_DEPENDENCIES_FILE" >&2
  exit 2
fi
source_dir=$1
deps_file=$2
if [ ! -f "$deps_file" ]; then
  echo "module dependency guard: missing $deps_file (reconfigure)" >&2
  exit 2
fi

modules='model:football_model
foundation:football_foundation
support:football_support
sim:football_sim
env:football_engine
ai:football_ai
app/input:football_app_input'
provides='football_foundation:foundation
football_model:model
football_sim_contracts:sim-contracts
football_support:support
football_animation:sim/animation
football_sim:sim
football_engine:env
football_ai:ai
football_app_support:app
football_app_input:app'
value_of() {
  printf '%s\n' "$1" | sed -n "s/^$2://p"
}
closure_of() {
  sed -n "s/^$1: //p" "$deps_file"
}
contract_headers=$(closure_of football_sim_contracts_headers)
if [ -z "$contract_headers" ]; then
  echo 'module dependency guard: missing sim contract header membership (reconfigure)' >&2
  exit 2
fi
classify() {
  case " $contract_headers " in
    *" $1 "*) echo sim-contracts; return ;;
  esac
  case "$1" in
    sim/animation/*) echo sim/animation ;;
    *) echo "${1%%/*}" ;;
  esac
}

status=0
for retired in control observation controller data sim/player/controller; do
  if [ -e "$source_dir/$retired" ]; then
    echo "module dependency guard: retired src/$retired directory" >&2
    status=1
  fi
done
for dir in "$source_dir"/*; do
  [ ! -d "$dir" ] && continue
  case "${dir##*/}" in
    foundation|support|model|sim|ai|env|app) ;;
    *) echo "module dependency guard: unsupported top-level module ${dir##*/}" >&2; status=1 ;;
  esac
done
for core in football_foundation football_model football_support football_animation football_sim football_ai football_engine; do
  for target in $(closure_of "$core"); do
    case "$target" in
      football_app_input|football_app_support|football_controller)
        echo "module dependency guard: forbidden $core -> $target link" >&2; status=1 ;;
    esac
  done
done
for target in $(closure_of football_sim); do
  case "$target" in
    football_sim|football_animation|football_support|football_foundation|football_model|football_sim_contracts) ;;
    *) echo "module dependency guard: forbidden football_sim -> $target link" >&2; status=1 ;;
  esac
done
for target in $(closure_of football_app_input); do
  case "$target" in
    football_app_input|football_ai|football_sim_contracts|football_foundation|football_model) ;;
    *) echo "module dependency guard: forbidden football_app_input -> $target link" >&2; status=1 ;;
  esac
done
for target in $(closure_of football_ai); do
  case "$target" in
    football_ai|football_sim_contracts|football_foundation|football_model) ;;
    *) echo "module dependency guard: forbidden football_ai -> $target link" >&2; status=1 ;;
  esac
done
# A convenient transitive edge must not turn passive contracts into runtime.
for target in $(closure_of football_sim_contracts); do
  case "$target" in
    football_sim_contracts|football_foundation|football_model) ;;
    *)
      echo "module dependency guard: forbidden football_sim_contracts -> $target link" >&2
      status=1 ;;
  esac
done

if grep -RqE 'Legacy(PlayerDecision|TeamDecision|DecisionFactories)|CreateDefault(ElizaDecision|TeamAIDecision)Factory' \
    --include='*.cpp' --include='*.hpp' --include='*.h' "$source_dir"; then
  echo 'module dependency guard: retired decision object API' >&2
  status=1
fi
for file in legacy_player_decision.cpp legacy_player_decision.hpp legacy_player_decision_factory.hpp legacy_team_decision.hpp legacy_team_decision_factory.hpp legacy_decision_factories.hpp; do
  if [ -e "$source_dir/sim/$file" ]; then
    echo 'module dependency guard: retired decision object file' >&2
    status=1
  fi
done
if [ -e "$source_dir/sim/ai_support/AIfunctions.hpp" ] ||
   [ -e "$source_dir/sim/ai_support/AIfunctions.cpp" ] ||
   grep -RqE 'AIfunctions|(^|[^[:alnum:]_])AI_[[:alnum:]_]+' \
     --include='*.cpp' --include='*.hpp' --include='*.h' "$source_dir/sim"; then
  echo 'module dependency guard: obsolete AIfunctions/AI_ API in sim' >&2
  status=1
fi
if grep -RqE 'TacticalBoard|PlayerDirective|PlannedPlayerRole|ObserveTactics|TeamRequests|TimedPlayerIntent|marking_target' \
    --include='*.cpp' --include='*.hpp' --include='*.h' "$source_dir/sim"; then
  echo 'module dependency guard: tactical intent leaked into simulation contracts' >&2
  status=1
fi

for file in humangamer.cpp humangamer.hpp team_tactical_state.hpp; do
  if [ -e "$source_dir/sim/$file" ]; then
    echo "module dependency guard: retired sim/$file file" >&2; status=1
  fi
done
if grep -RqiE 'HumanController|HumanGamer|PlayerController|ControllerInput|ExternalController|TeamTacticalState|ApplyAttackingRun|ApplyTeamPressure|ApplyKeeperRush|externally_controlled|attacking_run_remaining_ms|pressure_remaining_ms|keeper_rush_remaining_ms' \
    --include='*.cpp' --include='*.hpp' --include='*.h' "$source_dir"; then
  echo 'module dependency guard: retired input/tactical authority in core' >&2; status=1
fi

check_includes() {
  label=$1
  target=$2
  shift 2
  closure=$(closure_of "$target")
  if [ -z "$closure" ]; then
    echo "module dependency guard: $target missing from $deps_file" >&2
    exit 1
  fi
  allowed=$(value_of "$provides" "$target")
  for dependency in $closure; do
    prefix=$(value_of "$provides" "$dependency")
    [ -z "$prefix" ] || allowed="$allowed $prefix"
  done
  used=$(grep -Rh -o -E \
      '#[[:space:]]*include[[:space:]]*("[^"]*"|<(foundation|model|control|observation|controller|support|sim|env|app|ai)/[^>]*>)' \
      --include='*.cpp' --include='*.hpp' --include='*.h' "$@" 2>/dev/null \
    | sed -E 's/#[[:space:]]*include[[:space:]]*["<](.*)[">]/\1/' | sort -u)
  for header in $used; do
    case "$header" in
      ../*|./*|/*|*/../*|*/./*)
        echo "module dependency guard: $label non-canonical include $header" >&2
        status=1; continue ;;
    esac
    prefix=$(classify "$header")
    case " $allowed " in
      *" $prefix "*) ;;
      *)
        echo "module dependency guard: $label includes $header but $target does not declare it" >&2
        status=1 ;;
    esac
  done
}
for header in $contract_headers; do
  case "$header" in
    sim/*.hpp) ;;
    *) echo "module dependency guard: invalid contract header $header" >&2; exit 1 ;;
  esac
  if [ ! -f "$source_dir/$header" ]; then
    echo "module dependency guard: missing contract header $header" >&2
    exit 1
  fi
  check_includes "sim contract $header" football_sim_contracts "$source_dir/$header"
done
for entry in $modules; do
  module=${entry%%:*}
  target=${entry#*:}
  [ ! -d "$source_dir/$module" ] || check_includes "src/$module" "$target" "$source_dir/$module"
done
exit "$status"
