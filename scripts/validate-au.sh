#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
if [[ "$(uname -s)" != Darwin ]]; then echo "AU validation requires macOS." >&2; exit 1; fi
if /usr/bin/pgrep -x 'Logic Pro' >/dev/null 2>&1; then echo "Quit Logic Pro before running AU validation." >&2; exit 1; fi
artifact="${1:-build/PitchWorkbench_artefacts/Release/AU/Pitch Workbench.component}"
log_directory="${2:-build/validation}"
[[ -d "$artifact" ]] || { echo "Missing compiled Audio Unit." >&2; exit 1; }
destination="$HOME/Library/Audio/Plug-Ins/Components/Pitch Workbench.component"
work="$(mktemp -d "${TMPDIR:-/tmp}/pitchworkbench-au.XXXXXX")"
had_prior=0
restore() {
  result=$?
  trap - EXIT
  if [[ -e "$destination" || -L "$destination" ]]; then
    mv "$destination" "$work/validated.component" || { echo "Could not move temporary AU. Backup remains in $work" >&2; exit 1; }
  fi
  if [[ $had_prior -eq 1 ]]; then
    mv "$work/prior.component" "$destination" || { echo "Restore failed. Prior AU is in $work/prior.component" >&2; exit 1; }
  fi
  rm -rf "$work"
  exit "$result"
}
mkdir -p "$(dirname "$destination")" "$log_directory"
if [[ -e "$destination" || -L "$destination" ]]; then mv "$destination" "$work/prior.component"; had_prior=1; fi
trap restore EXIT
ditto "$artifact" "$destination"
codesign --force --sign - "$destination"
# Refresh the per-user registrar after introducing a new AU bundle.
killall -9 AudioComponentRegistrar 2>/dev/null || true
sleep 2
auval -a 2>&1 | tee "$log_directory/registered-aus.txt"
plutil -p "$destination/Contents/Info.plist" > "$log_directory/au-info.txt"
auval -v aufx PtWb Clvn 2>&1 | tee "$log_directory/auval.txt"
grep -q 'AU VALIDATION SUCCEEDED' "$log_directory/auval.txt"
