#!/usr/bin/env sh
set -eu
if [ "$#" -ne 3 ]; then
  echo "usage: $0 GUARD SOURCE_DIR MODULE_DEPENDENCIES_FILE" >&2
  exit 2
fi
guard=$1
source_dir=$2
report=$3
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
sh "$guard" "$source_dir" "$report"
reject() {
  if sh "$guard" "$source_dir" "$tmp/report" >"$tmp/log" 2>&1; then
    echo "source ownership guard accepted $1" >&2
    exit 1
  fi
  if ! grep -q "$2" "$tmp/log"; then
    echo "source ownership guard rejected $1 for the wrong reason" >&2
    cat "$tmp/log" >&2
    exit 1
  fi
}
sed '/^football_ai_sources:/s@$@ sim/match.cpp@' "$report" >"$tmp/report"
reject 'actor implementation compiled into AI' 'foreign source'
sed '/^football_app_input_sources:/s@$@ ai/default_ai.cpp@' "$report" >"$tmp/report"
reject 'concrete policy compiled into input' 'foreign source'
sed '/^football_app_input_sources:/s@$@ app/fixtures/default_teams.cpp@' "$report" >"$tmp/report"
reject 'fixtures compiled into value-only input' 'foreign source'
sed '/^football_foundation_sources:/s@$@ foundation/math/scalar.cpp@' "$report" >"$tmp/report"
reject 'duplicate source owner' 'duplicate owner'
sed '/^football_ai_sources:/s@$@ ai/missing.cpp@' "$report" >"$tmp/report"
reject 'missing source file' 'missing football_ai source'
sed '/^football_ai_sources:/s@$@ ai/../sim/match.cpp@' "$report" >"$tmp/report"
reject 'relative-path ownership escape' 'non-canonical'
sed '/^football_sim_sources:/s@$@ sim/world_state.hpp@' "$report" >"$tmp/report"
reject 'runtime taking contract ownership' 'runtime owns contract'
sed '/^football_sim_sources:/s@$@ sim/animation/library.cpp@' "$report" >"$tmp/report"
reject 'runtime taking independent animation ownership' 'runtime owns animation'
sed '/^football_sim_contracts_sources:/s@$@ sim/gamedefines.cpp@' "$report" >"$tmp/report"
reject 'implementation in passive contracts' 'non-exported source'
sed '/^football_ai_sources:/d' "$report" >"$tmp/report"
reject 'missing target metadata' 'missing football_ai source/header metadata'
cp "$report" "$tmp/report"
printf 'football_hidden_sources: ai/default_ai.cpp\n' >>"$tmp/report"
reject 'unapproved hidden module target' 'unsupported module target'
