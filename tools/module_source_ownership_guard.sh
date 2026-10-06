#!/usr/bin/env sh
# Verify compiled sources and declared headers, not a second source inventory.
set -eu
if [ "$#" -ne 2 ] || [ ! -d "$1" ] || [ ! -f "$2" ]; then
  echo "usage: $0 SOURCE_DIR MODULE_DEPENDENCIES_FILE" >&2
  exit 2
fi
source_dir=$1
report=$2
owners=$(mktemp)
trap 'rm -f "$owners"' EXIT HUP INT TERM
status=0
required='football_foundation football_model football_support football_sim_contracts football_animation football_sim football_ai football_engine football_app_support football_app_input'
for target in $required; do
  if ! grep -q "^${target}_sources: ." "$report"; then
    echo "source ownership guard: missing $target source/header metadata (reconfigure)" >&2
    status=1
  fi
done
contract_headers=$(sed -n 's/^football_sim_contracts_headers: //p' "$report")
for target in $(sed -n 's/^\([^: ]*\)_sources: .*/\1/p' "$report"); do
  case "$target" in
    football_foundation) prefix=foundation ;;
    football_model) prefix=model ;;
    football_support) prefix=support ;;
    football_sim|football_sim_contracts) prefix=sim ;;
    football_animation) prefix=sim/animation ;;
    football_ai) prefix=ai ;;
    football_engine) prefix=env ;;
    football_app_support|football_app) prefix=app ;;
    football_app_input) prefix=app/input ;;
    *) echo "source ownership guard: unsupported module target $target" >&2; status=1; continue ;;
  esac
  for file in $(sed -n "s/^${target}_sources: //p" "$report"); do
    case "$file" in
      /*|../*|./*|*/../*|*/./*)
        echo "source ownership guard: non-canonical $target source $file" >&2
        status=1; continue ;;
    esac
    case "$file" in
      "$prefix"/*) ;;
      *) echo "source ownership guard: $target owns foreign source $file" >&2; status=1 ;;
    esac
    if [ "$target" = football_sim_contracts ]; then
      case " $contract_headers " in
        *" $file "*) ;;
        *) echo "source ownership guard: contracts own non-exported source $file" >&2; status=1 ;;
      esac
    elif [ "$target" = football_sim ]; then
      case "$file" in
        sim/animation/*)
          echo "source ownership guard: runtime owns animation source $file" >&2; status=1 ;;
      esac
      case " $contract_headers " in
        *" $file "*) echo "source ownership guard: runtime owns contract header $file" >&2; status=1 ;;
      esac
    fi
    if [ ! -f "$source_dir/$file" ]; then
      echo "source ownership guard: missing $target source $file" >&2
      status=1
    fi
    printf '%s %s\n' "$file" "$target" >>"$owners"
  done
done
if ! awk 'seen[$1]++ { print "source ownership guard: duplicate owner for " $1 > "/dev/stderr"; failed=1 } END { exit failed }' "$owners"; then
  status=1
fi
exit "$status"
