# Observed verification — version 0.2, 6 November 2026

This report separates observed passes from source-level provisions that have
not yet run. A full working Logic plug-in has **not** been established.

## Version 0.2 review and installer work

The current audio core was recompiled and rerun after these changes. The
detector now covers the 55 Hz boundary; regression tests verify unchanged
notes have zero manual shift and edited notes use a constant pitch offset.
Manual playback no longer intentionally flattens the measured pitch movement.
The JUCE processor integration of this behavior remains a pending Mac check.

Selection and undo references are guarded by a take revision, capture notices
remain visible long enough to read, and transport bookkeeping resets when
the processor is prepared again.

Eight portable installer tests pass: component choices and required source,
standard paths, metadata validation, rejection of missing/text/mismatched
bundles, non-relocatable upgrade settings, dependency source inclusion,
missing-source rejection, and symlink rejection. Shell syntax and Python
compilation checks pass. The packaging command was also checked on Linux: it
refuses the platform and creates no installer or output directory.

The native pipeline now builds an Installer.app product package with AU/VST3
choices, stable package IDs, source/documentation support payload, signed
binary hashes, optional Developer ID signing, notarization and stapling.
It requires three registered/enabled CTest suites, repeats those checks,
runs auval, checks arm64 and library dependencies, and verifies signatures
before publishing a package. A new native VST3 harness loads the actual
compiled VST3 and tests rendering, parameters, state and editor construction.

**These native build and packaging steps have not executed here. No `.pkg`,
signed binary, notarization ticket, or observed Logic compatibility exists
from this session.** The included Mac build command and workflow are provided
to perform them. See INSTALLER.md.

## Environment actually used

- Linux x86-64; GCC with C++17.
- Installed Rubber Band runtime: `3.3.0+dfsg-2build1`.
- The test compatibility declarations in `RubberBandABI.h` were used because
  the environment lacks Rubber Band development headers.
- The Mac build is configured for JUCE 8.0.6 and Rubber Band 4.0.0 with vendor
  headers. That dependency/build combination remains unverified here. The
  Mac script reruns the tests against the actual compiled dependencies.
- GitHub downloads are blocked in this environment, and no macOS SDK, Logic,
  Audio Unit validator, or JUCE installation is available here.

## Three checks performed on the audio core

| Check | Observed result | Limit |
|---|---|---|
| C++ functional tests | Pass | Core helpers and real Rubber Band processing, not the JUCE host wrapper |
| Independent FFT analysis | Pass | Two steady synthetic renders at 48 kHz, not a subjective vocal evaluation |
| AddressSanitizer + UndefinedBehaviorSanitizer | Pass with leak detection disabled | Our compiled test/core code instrumented; external Rubber Band binary is not instrumented |

LeakSanitizer could not inspect the sandbox's process/thread information and
terminated before reporting. The suite was rerun successfully with
`ASAN_OPTIONS=detect_leaks=0`. This is **not** a passed memory-leak test.

The functional suite checks:

- Nearest-note scale membership, including negative pitch classes.
- Pitch detection at 44.1, 48, 88.2, and 96 kHz for 55, 65.406, 110, 220, 440,
  and 880 Hz, plus a harmonic-rich 173 Hz signal with a stronger overtone.
  Detector error must stay below 8 cents.
- Rejection of digital silence, DC, and seeded white noise.
- Retune smoothing and bounded shift ratios.
- Segmentation, edited-note lookup, interval boundaries, seek lookup, and
  clearing a take.
- Actual automatic correction and manual three-semitone shifts at 44.1, 48,
  and 96 kHz. The acceptance threshold is 12 cents after settling.
- Finite output and matching output for identical stereo inputs.
- An impulse test at 48 kHz: measured delay **6144 samples**, exactly equal to
  the reported delay (128 ms at that sample rate).
- Eight-second stereo stress runs at 44.1, 48, and 96 kHz, changing between
  one-octave-down, one-octave-up, unity, and a fractional ratio every half
  second. No adapter underruns or overflows occurred.

## Measured steady pitch results

The C++ detector measured the following output pitch errors:

| Sample rate | Automatic to A4 | Manual A3 to C4 |
|---|---:|---:|
| 44.1 kHz | +0.901 cents | +0.140 cents |
| 48 kHz | +0.071 cents | +0.277 cents |
| 96 kHz | +0.068 cents | +0.276 cents |

Independent FFT analysis of the 48 kHz WAVs measured approximately:

| Render | Target | FFT estimate | Error |
|---|---:|---:|---:|
| Automatic A4 | 440.000 Hz | 439.767 Hz | −0.918 cents |
| Manual C4 | 261.626 Hz | 261.610 Hz | −0.103 cents |

These methods measure differently in the presence of spectral modulation.
Both pass the unchanged 12-cent criterion. These results establish accurate
settled pitch on the tested tones, not transparent tuning on arbitrary vocals.
See `evidence/` for the raw logs and JSON. WAVs in `tests/` are synthetic
verification audio, not voice recordings.

## Failures caught and corrected

The initial output adapter requested more frames than Rubber Band had
available. It now retrieves only the available count. The initial R2 mode
also missed a manual target by about 20 cents. Switching to R3 Finer brought
the measured manual error below one cent on these tests. The pitch acceptance
threshold was not loosened. A scale test initially had an incorrect expected
nearest note; its expectation was corrected to A for a pitch closer to A
than G in A natural minor.

Manual capture now keeps the measured pitch initially; it does not silently
snap the entire take. Editor moves and restored targets are limited to the
engine's one-octave range. Undo history is tagged with a take revision so it
cannot apply old edits to a replacement capture.

## Remaining gates — NOT RUN

| Gate | Status |
|---|---|
| JUCE/AU/VST3 compilation on macOS arm64 | Pending |
| Rubber Band 4.0.0 static build and linking | Pending |
| C++ processor/host harness in `tests/host_tests.cpp` | Pending |
| Actual VST3 binary harness in `tests/format_tests.cpp` | Pending |
| pkgbuild/productbuild execution and installer choices on macOS | Pending |
| Clean Mac installation, system paths and upgrade behavior | Pending |
| Developer ID signing, notarization and stapling | Pending |
| GUI construction, rendering, dragging, resize and accessibility | Pending |
| Apple `auval` | Pending |
| Logic registration and loading | Pending |
| Actual Logic capture, manual replay, seeking and loops | Pending |
| Logic project save, quit, reopen and edit restoration | Pending |
| Compensated Logic playback and offline bounce alignment | Pending |
| Real vocals, naturalness, consonants, vibrato and onset handling | Pending |
| CPU load, long sessions, multiple instances, live monitoring | Pending |
| macOS memory leaks and realtime allocation profiling | Pending |

The Mac build, tests, installation/validation scripts, and Logic acceptance
document are provided to run these gates. They are not reported as passed.
