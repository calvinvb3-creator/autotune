# Pitch Workbench 0.3

AU and VST3 monophonic pitch correction and captured-audio note editing for Apple silicon Macs, macOS 12 or later.

## Workflow

Install the compiled .pkg with Logic closed. In Logic choose Audio FX → Audio Units → Independent Audio Development → Pitch Workbench.

Automatic mode tunes the incoming vocal to a key and scale, with retune time and correction amount controls. For detailed editing, clear the take, click Capture, play one continuous pass, and click Stop capture. Switch to Note editor. Captures are limited to five minutes; seeking or looping during capture stops capture with a visible message.

In Note editor choose Pitch tool, Move in time, or Change length. Drag a note; Alt-drag permits fine pitch changes. Shift-click selects multiple notes; Cmd-A selects all. Up/down arrows move selected pitches; Alt-arrow moves cents. Cmd-Z undoes; Cmd-Shift-Z redoes.

Split divides the selected note at its midpoint. Merge joins it to the following source-contiguous note. Correct snaps selected notes to the chosen scale. Reset restores original timing and pitch and clears inspector changes. The inspector controls level, formants, modulation, linear drift, fades and mute. Modulation/drift are based on a measured pitch curve and linear trend; these are not claimed to reproduce Celemony's analysis. Notes can overlap after time moves and their audio is summed. Avoid overload when adding gain or overlaps.

Edits render on a background thread; wait for Rendering edits to finish. Until then the prior rendered take remains audible. Rendering errors are displayed. Source audio, note edits and settings are embedded in DAW session state. Export WAV saves the rendered take at its original sample rate as 24-bit PCM, starting at the captured take's origin. Place the exported file at that same timeline position. Automatic Correction and Retune controls apply to Automatic mode; captured edits use the individual note controls.

Five minutes of stereo audio at 96 kHz can consume hundreds of megabytes while capturing. Embedded audio also increases project size and save times. Clear removes this plug-in instance's captured audio and edits; undo cannot restore a cleared take.

## Validation and release status

The build runs engine and processor tests, offline edit audio tests at 44.1/48/96 kHz in mono/stereo, independent FFT/Hilbert measurements, binary VST3 tests, and Apple AU validation. It packages the binaries using Apple tools, installs the actual package on the runner, and rechecks installed plug-ins.

This is an unsigned development build. It is not yet a signed/notarized public release, and no Melodyne feature or sound-quality equivalence is claimed. See docs/RELEASE_ACCEPTANCE.md for remaining release gates.

## Building and licenses

The GitHub Actions workflow builds and tests on a native Apple silicon macOS runner. Developers may also run bash scripts/build-mac.sh followed by python3 scripts/package_macos.py. Release packaging requires Developer ID Application and Installer identities plus a notarytool profile; scripts/package_macos.py --release enforces these and notarizes the package.

Our original source is MIT licensed. The linked JUCE and Rubber Band libraries have their own license terms. The installer includes their corresponding source; see those included licenses. No association with Celemony or Antares is claimed.
