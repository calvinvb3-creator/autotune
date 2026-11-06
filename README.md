# Pitch Workbench 0.2 — Mac plug-in and installer project

Native C++ **Audio Unit for Logic Pro** and **VST3 for compatible hosts**, with
a standard macOS Installer.app `.pkg` build. Apple silicon, macOS 12 or newer.

**This archive contains source and an installer build, not a compiled `.pkg`.**
The audio core and portable packaging tests pass on Linux. Mac compilation,
native format tests, Apple validation, installer execution and Logic playback
have not run here. A Mac must run those gates before the installer exists.

## Build the installer

1. Extract this folder on your M-series Mac. Quit Logic.
2. If missing, install Apple's Command Line Tools (`xcode-select --install`)
   and CMake (`brew install cmake` if you use Homebrew).
3. Double-click **Build-Installer.command**, or run:

   ```bash
   bash Build-Installer.command
   ```

The first build downloads pinned JUCE 8.0.6 and Rubber Band 4.0.0 from GitHub.
It compiles AU/VST3, tests the engine and host processor, loads and tests the
actual VST3 binary, and runs Apple's AU validator. AU validation temporarily
places the component in your user plug-in folder, then restores the previous
component. Keep Logic closed during validation.

If the checks pass, it builds:

`dist/PitchWorkbench-0.2.0-macOS-arm64-Local-Unsigned.pkg`

Open the `.pkg` for the normal installation wizard: introduction, license,
AU/VST3 choices, administrator authorization, installation and completion.
Both formats install to their standard system folders. End users of a built
package do not need CMake, Xcode, Homebrew or a compiler.

The local package is unsigned and not notarized; it is a development build,
not a public release. The script does not disable Gatekeeper or remove
quarantine attributes. No placeholder package is created after failed gates.
An existing package is never silently overwritten.

Documentation and corresponding source, including the dependencies, install
under `/Library/Application Support/Pitch Workbench/`. Build errors are logged
to `build/installer-build.log`.

## Signed and notarized release

Use your Developer ID Application and Developer ID Installer certificates
already in your Mac's Keychain, plus a stored notarytool Keychain profile.
Keep private keys and credentials on your Mac; do not send them in chat.

```bash
bash scripts/build-mac.sh
python3 scripts/package_macos.py --release \
  --app-identity 'Developer ID Application: Your Name (TEAMID)' \
  --installer-identity 'Developer ID Installer: Your Name (TEAMID)' \
  --notary-profile 'YourStoredKeychainProfile'
```

Release mode signs both bundles and the package, submits to Apple, staples
and validates the notarization ticket, and checks Gatekeeper. Failed steps
prevent publication to `dist/`. No signing identities are included here.

An optional manually triggered GitHub Actions workflow builds on a native
macOS runner and saves the local package and logs as artifacts. It has been
written, not run. Put this folder's contents at your repository root to use it.

## Use

In Logic: Audio FX → Audio Units → Independent Audio Development → Pitch
Workbench. Logic uses the AU. Other compatible hosts can scan the VST3.

- **Automatic:** choose a key/scale and adjust retune time and correction.
- **Note editor:** Capture notes, play one pass, stop capture, click Fit take,
  move a note and replay the same region. Shift-drag gives fine adjustment;
  arrow keys move by semitones; double-click restores measured pitch.
- Manual moves apply a constant offset, preserving pitch modulation. Untouched
  notes keep their movement. Moves are limited to one octave in either direction.
- Save the host project to save edits. Original audio remains in the host.

Start with an isolated dry mono vocal. Do not loop while capturing. Notes use
absolute host times: changing source timing requires clearing and recapturing.
The maximum is 4096 segments per take. Clear take discards captured edits.

## Scope

This is a monophonic development version, not Melodyne feature parity. It has
automatic scale correction, manual pitch moves, formant preservation in its
shifting library, saved state, mono/stereo buses and delayed dry bypass.

Polyphonic separation, ARA, note split/merge, time/duration editing, editable
formants, independent drift/modulation controls, MIDI guidance, curve drawing
and independent audio transfer/export are not implemented. Manual boundaries
are approximate. Stereo analysis sums the channels and cannot separate singers.

Latency is about **125–128 ms**. This version is for playback editing;
low-latency live monitoring needs further work. Synthetic-tone accuracy does
not establish transparent processing on all real vocals.

## Verification

- [Observed passes and pending checks](docs/VALIDATION.md)
- [Installer and release details](docs/INSTALLER.md)
- [Logic acceptance checks](docs/LOGIC_ACCEPTANCE.md)

Portable packaging checks:

```bash
python3 -m unittest discover -s tests -p 'test_*.py' -v
```

Independent spectral verification, with NumPy/SciPy installed:

```bash
python3 tests/verify_audio.py build
```

## Licenses and identity

Our original code is MIT licensed. JUCE and Rubber Band have separate
AGPL/GPL/commercial options. The MIT notice does not relicense a combined
plug-in as a permissive closed-source product. The package includes
corresponding source; review applicable terms before product distribution.

Pitch Workbench is a provisional name. Code, interface and assets are original;
no Celemony or Antares code/branding is included. No trademark, copyright or
patent clearance is claimed.

Primary references: [Apple packaging](https://developer.apple.com/documentation/xcode/packaging-mac-software-for-distribution),
[Apple notarization](https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution),
[JUCE licensing](https://juce.com/get-juce/),
[Rubber Band](https://breakfastquay.com/rubberband/).
