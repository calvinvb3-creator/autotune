#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p build
exec > >(tee build/installer-build.log) 2>&1
trap 'result=$?; if [[ $result -ne 0 ]]; then echo "Build stopped. See build/installer-build.log for the error."; fi; read -r -p "Press Return to close."; exit "$result"' EXIT
bash scripts/build-mac.sh
python3 -m unittest discover -s tests -p 'test_*.py'
python3 scripts/package_macos.py
open dist
