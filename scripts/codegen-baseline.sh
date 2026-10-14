#!/usr/bin/env bash
# Regenerate the hint-free baseline codegen used as the harvester's --generated default.
# Usage: scripts/codegen-baseline.sh
set -eu
cd "$(dirname "$0")/.."
SDK=${REXGLUE_SDK:-C:/personal/tools/rexglue/0.10.0.9/win-amd64}
mkdir -p out/baseline out/logs
sed -e 's|^[[:space:]]*game_root =.*|game_root = "../../game"|' \
    -e 's|^[[:space:]]*file_path =.*|file_path = "../../game/default.xex"|' \
    -e 's|^[[:space:]]*out_directory_path =.*|out_directory_path = "default"|' \
    -e 's|^[[:space:]]*includes =.*|includes = []|' \
    gow2_manifest.toml > out/baseline/gow2_manifest.toml
(cd out/baseline && "$SDK/bin/rexglue.exe" codegen gow2_manifest.toml) > out/logs/codegen-baseline.stdout 2>&1
