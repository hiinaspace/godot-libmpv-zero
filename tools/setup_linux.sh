#!/usr/bin/env bash
set -euo pipefail
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$repo_dir"
git submodule update --init godot-cpp
mkdir -p .local
nix build .#steam-audio --out-link .local/steam-audio --cores "${LIBMPV_ZERO_BUILD_JOBS:-6}"
addon_dir=project/addons/godot-steam-audio
mkdir -p "$addon_dir/bin" "$addon_dir/icons"
cp -f .local/steam-audio/bin/* "$addon_dir/bin/"
cp -f .local/steam-audio/icons/* "$addon_dir/icons/"
nix develop --command bash tools/build_linux.sh
nix develop --command godot --headless --xr-mode off --path project --editor --import
