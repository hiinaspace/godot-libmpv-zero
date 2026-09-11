#!/usr/bin/env bash
set -euo pipefail
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build_dir=${LIBMPV_ZERO_BUILD_DIR:-${XDG_CACHE_HOME:-$HOME/.cache}/godot-libmpv-zero/build}
cmake -S "$repo_dir" -B "$build_dir" -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DMPV_DIR="${MPV_DIR:?Run inside nix develop}" \
  -DGODOTCPP_TARGET=template_debug -DGODOTCPP_BUILD_PROFILE="$repo_dir/build_profile.json"
cmake --build "$build_dir" --parallel "${LIBMPV_ZERO_BUILD_JOBS:-6}"
