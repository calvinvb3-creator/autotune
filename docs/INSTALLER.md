# Native macOS installer

The user-facing product is a `.pkg` opened by Apple's Installer.app. The `.pkg`
is created only after real native bundles exist and the Mac checks pass.

| Component | Installed location |
|---|---|
| AU | `/Library/Audio/Plug-Ins/Components/Pitch Workbench.component` |
| VST3 | `/Library/Audio/Plug-Ins/VST3/Pitch Workbench.vst3` |
| Docs, build manifest and corresponding source | `/Library/Application Support/Pitch Workbench/` |

Installation is system-wide and requires the normal administrator prompt.
AU and VST3 are selectable; documentation and source are mandatory support
files. Package identifiers are stable across upgrades: `local.pitchworkbench.au`,
`local.pitchworkbench.vst3`, and `local.pitchworkbench.support`. Bundle
relocation is disabled, and upgrades replace the corresponding installed
bundle rather than overlaying stale files. Quit Logic before installation.

An old prototype under your personal `~/Library/Audio/Plug-Ins/` can shadow a
system copy. Move it aside before installing. The installer does not delete
personal plug-ins.

## Gates before a package appears

1. Version and Info.plist identity match; executable exists and has a Mach-O
   header. Metadata inspection is not the complete binary validation.
2. All three CTest suites are registered, enabled, and pass: engine, processor
   host, and native VST3 binary loading/rendering/state/editor construction.
3. Apple's `auval` succeeds on the compiled AU. Its temporary installation is
   reverted, including restoration of any prior component.
4. `lipo` verifies arm64; `otool` rejects unexpected external dependencies.
   Rubber Band is linked statically, not through a Homebrew library path.
5. Both staged plug-ins are signed and their signatures verify.
6. `pkgbuild` creates payload packages with non-relocatable bundles.
7. `productbuild` creates the component-choice wizard. `pkgutil` expands the
   result and `installer -showChoicesXML` checks its choices.
8. In release mode the package is signed with Developer ID Installer,
   notarized, stapled, and assessed by Gatekeeper. Only then is it copied to
   `dist/`, alongside a SHA-256 file.

The installed Build-Manifest.json records signed executable hashes and paths.
The original and dependency sources are included in a corresponding-source
archive; CMake can use its ThirdParty directories instead of downloading them.
These are implemented gates, not a statement that they have run here.

## Local versus public builds

The default package is named `Local-Unsigned.pkg`. Its plug-ins use ad-hoc
development signatures. The package is unsigned and not notarized; downloaded
copies may require macOS approval and are not public releases.

`--release` requires distinct Developer ID Application and Developer ID
Installer identities plus a notarytool Keychain profile. The script rejects
missing or incorrectly typed identity names. Apple's tools validate the actual
certificates and submission. Do not disable Gatekeeper to bypass a failure.

## Removal

Quit audio hosts. In Finder, use Go → Go to Folder to open the plug-in paths
above and move only the Pitch Workbench bundles to Trash. Remove the support
folder if you also want to remove documentation and source. Existing Logic
projects and recordings are not touched. Receipts may remain; they do not
load plug-ins or consume audio-processing resources.

## Pending native checks

No `.pkg` was produced in the Linux development session. macOS compilation,
native validation, installer launch, upgrades, signing/notarization and Logic
playback must be observed on a Mac. The scripts and optional workflow automate
those checks; they are not substitutes for observed results.
