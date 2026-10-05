#!/usr/bin/env sh
# Declared CMake edges are documentation only: every target compiles with
# -I src, because headers cross-reference module/... paths, so an undeclared
# include still builds. This guard enforces the dangerous direction - a module
# may only include project prefixes covered by its own target's transitive
# link closure, as exported by CMake into module_dependencies.txt.
#
# "Declared but unused" edges are deliberately not reported here; the compiler
# cannot see them either, so dropping one stays a review item.
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

# module directory -> the target that compiles it. src/app is out of scope:
# it is executable-only and may use any core module.
modules='model:football_model
foundation:football_foundation
observation:football_observation
control:football_control
controller:football_controller
support:football_support
sim:football_sim
env:football_engine
ai:football_ai'

# target -> the include prefix it provides
provides='football_foundation:foundation
football_model:model
football_observation:observation
football_control:control
football_controller:controller
football_support:support
football_animation:sim/animation
football_sim:sim
football_engine:env
football_ai:ai
football_app_support:app'

value_of() {
  # value_of <multiline table> <key>
  printf '%s\n' "$1" | sed -n "s/^$2://p"
}

status=0
for entry in $modules; do
  module=${entry%%:*}
  target=${entry#*:}
  [ -d "$source_dir/$module" ] || continue

  closure=$(sed -n "s/^$target: //p" "$deps_file")
  if [ -z "$closure" ]; then
    echo "module dependency guard: $target missing from $deps_file" >&2
    exit 1
  fi

  # Own prefix plus every prefix provided by a target in the closure.
  allowed=$(value_of "$provides" "$target")
  for dependency in $closure; do
    prefix=$(value_of "$provides" "$dependency")
    [ -n "$prefix" ] && allowed="$allowed $prefix"
  done

  # Project prefixes actually included anywhere under this module. The awk
  # split resolves sim/animation before the broader sim root.
  used=$(grep -Rh -o -E \
      '#include "(foundation|model|observation|control|controller|support|sim|env|app)/[^"]*"' \
      --include='*.cpp' --include='*.hpp' --include='*.h' \
      "$source_dir/$module" 2>/dev/null \
    | sed -E 's/#include "(.*)"/\1/' \
    | awk -F/ '{ print ($1 == "sim" && $2 == "animation") ? "sim/animation" : $1 }' \
    | sort -u)

  for prefix in $used; do
    case " $allowed " in
      *" $prefix "*) ;;
      *)
        echo "module dependency guard: src/$module includes $prefix/ but $target does not declare it" >&2
        status=1
        ;;
    esac
  done
done

exit "$status"
