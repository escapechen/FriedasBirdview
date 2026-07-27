#!/usr/bin/env bash
set -euo pipefail

project_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
build_dir="$project_dir/build"
install_prefix="${1:-$HOME/.local}"

cmake -S "$project_dir" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release
cmake --build "$build_dir" --parallel
cmake --install "$build_dir" --prefix "$install_prefix"

printf 'Installed FriedasBirdview below %s. Start it from Plasma or run friedasbirdview.\n' "$install_prefix"
