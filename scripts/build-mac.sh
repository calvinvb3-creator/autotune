#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
if [[ "$(uname -s)" != Darwin ]]; then
  echo "This script builds a native macOS plug-in. Run it on your Mac." >&2
  exit 1
fi
if [[ "$(uname -m)" != arm64 ]]; then
  echo "Open a native Apple silicon Terminal, then run this script again." >&2
  exit 1
fi
if ! xcrun --find clang++ >/dev/null 2>&1; then
  echo "Install Apple's developer tools first: xcode-select --install" >&2
  exit 1
fi
if ! command -v cmake >/dev/null 2>&1; then
  echo "Install CMake, then retry. With Homebrew: brew install cmake" >&2
  exit 1
fi
mkdir -p build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0
cmake --build build --config Release --target PitchWorkbench_AU PitchWorkbench_VST3 pw_core_tests pw_offline_tests pw_host_tests pw_format_tests --parallel 4
codesign --force --sign - "build/PitchWorkbench_artefacts/Release/VST3/Pitch Workbench.vst3"
ctest --test-dir build -C Release --output-on-failure --no-tests=error
bash scripts/validate-au.sh
echo "Build, native VST3 checks and AU validation passed. Create the installer with: python3 scripts/package_macos.py"
