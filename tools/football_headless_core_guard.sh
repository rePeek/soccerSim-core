#!/usr/bin/env sh
# Keep the headless-core boundary mechanical: the shared object may not
# acquire a graphics dependency, and core source may not include the retired
# engine. The same mechanical guard now keeps Boost from creeping back in.
set -eu

if [ "$#" -ne 2 ]; then
  echo "usage: $0 LIBGAME SOURCE_DIR" >&2
  exit 2
fi

library=$1
source_dir=$2
forbidden_needed='Shared library: \[(lib(SDL2|EGL|GLX|OpenGL|GL\.so|X11)[^]]*)\]'
forbidden_include='^[[:space:]]*#[[:space:]]*include[[:space:]]*[<"][^">]*(SDL|EGL|GLX|OpenGL|GL/|systems/graphics|scene/scene2d|scene/scene3d|utils/gui2|menu/)[^">]*[">]'
forbidden_boost_needed='Shared library: \[libboost[^]]*\]'
forbidden_boost_include='^[[:space:]]*#[[:space:]]*include[[:space:]]*[<"]boost/.*[">]'

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

if readelf -d "$library" | grep -E "$forbidden_boost_needed"; then
  echo "headless-core guard: forbidden Boost NEEDED entry in $library" >&2
  exit 1
fi

if grep -R -n -E \
    --include='*.cpp' --include='*.hpp' --include='*.h' \
    "$forbidden_boost_include" "$source_dir"; then
  echo "headless-core guard: Boost include in $source_dir" >&2
  exit 1
fi