#!/bin/bash
set -euo pipefail
# CI-only preparation. Secrets are read from the environment and never echoed.
for required in PW_APP_CERTIFICATE_P12 PW_INSTALLER_CERTIFICATE_P12 PW_CERTIFICATE_PASSWORD PW_APP_SIGNING_IDENTITY PW_INSTALLER_SIGNING_IDENTITY PW_APPLE_ID PW_APPLE_TEAM_ID PW_APPLE_APP_PASSWORD RUNNER_TEMP GITHUB_ENV; do
  if [[ -z "${!required:-}" ]]; then echo "Missing release configuration: $required" >&2; exit 1; fi
done
if [[ "$(uname -s)" != Darwin ]]; then echo 'Signing requires macOS.' >&2; exit 1; fi
signing_directory="$(mktemp -d "$RUNNER_TEMP/pitchworkbench-signing.XXXXXX")"
keychain="$signing_directory/release.keychain-db"
key_password="$(openssl rand -hex 32)"
export PW_SIGNING_DIRECTORY="$signing_directory"
python3 - <<'PY'
import base64, os
from pathlib import Path
root=Path(os.environ['PW_SIGNING_DIRECTORY'])
for key,name in [('PW_APP_CERTIFICATE_P12','application.p12'),('PW_INSTALLER_CERTIFICATE_P12','installer.p12')]:
    p=root/name
    p.write_bytes(base64.b64decode(os.environ[key],validate=True))
    p.chmod(0o600)
PY
security create-keychain -p "$key_password" "$keychain"
security set-keychain-settings -lut 21600 "$keychain"
security unlock-keychain -p "$key_password" "$keychain"
security import "$signing_directory/application.p12" -P "$PW_CERTIFICATE_PASSWORD" -k "$keychain" -T /usr/bin/codesign
security import "$signing_directory/installer.p12" -P "$PW_CERTIFICATE_PASSWORD" -k "$keychain" -T /usr/bin/productsign -T /usr/bin/productbuild
security set-key-partition-list -S apple-tool:,apple:,codesign: -s -k "$key_password" "$keychain" >/dev/null
security list-keychains -d user -s "$keychain" "$HOME/Library/Keychains/login.keychain-db"
security default-keychain -s "$keychain"
xcrun notarytool store-credentials PitchWorkbench-CI --apple-id "$PW_APPLE_ID" --team-id "$PW_APPLE_TEAM_ID" --password "$PW_APPLE_APP_PASSWORD" --keychain "$keychain"
rm -f "$signing_directory/application.p12" "$signing_directory/installer.p12"
{
  printf 'PW_NOTARY_PROFILE=PitchWorkbench-CI\n'
  printf 'PW_CI_SIGNING_DIRECTORY=%s\n' "$signing_directory"
} >> "$GITHUB_ENV"
