#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
if [[ "$(uname -s)" != Darwin ]]; then echo "Run this on your Mac." >&2; exit 1; fi
artifact="build/PitchWorkbench_artefacts/Release/AU/Pitch Workbench.component"
if [[ ! -d "$artifact" ]]; then echo "Build first: bash scripts/build-mac.sh" >&2; exit 1; fi
# A staged component must have passed both compiled test executables.
ctest --test-dir build -C Release --output-on-failure --no-tests=error
destination="$HOME/Library/Audio/Plug-Ins/Components/Pitch Workbench.component"
backup="build/component-backup-$(date +%Y%m%d-%H%M%S)-$$"
mkdir -p "$(dirname "$destination")"
had_prior=0
if [[ -e "$destination" ]]; then
  mkdir -p "$backup"
  mv "$destination" "$backup/Pitch Workbench.component"
  had_prior=1
fi
rollback() {
  result=$?
  if [[ $result -ne 0 ]]; then
    mkdir -p "$backup"
    if [[ -e "$destination" ]]; then mv "$destination" "$backup/Rejected Pitch Workbench.component"; fi
    if [[ $had_prior -eq 1 ]]; then ditto "$backup/Pitch Workbench.component" "$destination"; fi
    echo "Installation/validation failed. Prior component restored when present. See $backup." >&2
  fi
}
trap rollback EXIT
ditto "$artifact" "$destination"
codesign --force --deep --sign - "$destination"
codesign --verify --deep --strict "$destination"
mkdir -p build/validation
auval -v aufx PtWb Clvn 2>&1 | tee build/validation/auval.txt
grep -q 'AU VALIDATION SUCCEEDED' build/validation/auval.txt
trap - EXIT
echo "Audio Unit installed and auval passed. Open Logic and finish docs/LOGIC_ACCEPTANCE.md."
