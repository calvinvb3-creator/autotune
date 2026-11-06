# Public release gates

A passing automated build is necessary but does not establish that this product is ready for public sale or equivalent to Melodyne.

Completed implementation in 0.3: captured-audio note playback; pitch, time and duration editing; midpoint split and source-contiguous merge; multi-selection; undo/redo; gain/mute, formant scale, modulation and linear-drift factors; edge fades; embedded source-audio state; WAV export. Each feature must retain its associated passing tests.

Remaining gates:

- Exercise actual Logic sessions with recorded male/female vocals, breath noise, sibilants, vibrato, slides, transients, multiple sample rates, offline bounce, project reopening, tempo/region changes and system restarts. AU validation alone is insufficient.
- Audit real-time safety, host state loading during playback, memory use on long takes, source replacement, cancellation and concurrent edits. Profile CPU and audio dropouts on supported hardware.
- Listen blind to real-vocal edits against the unedited recording and commercial references. Synthetic sine and harmonic tests cannot establish transparent vocal quality.
- ARA integration, elastic intra-note timing, independent sibilant tools, nonlinear drift analysis, pitch transitions and polyphonic editing are not implemented. These prevent a claim of near-complete Melodyne equivalence.
- Captured time follows absolute host seconds. Automatic relocation after Logic region edits and tempo changes requires a source/document integration design and tests; no such behavior is claimed.
- UI note editing, file export, accessibility, keyboard operation and undo histories require real-user acceptance and a visual review on macOS at multiple display scales.
- Sign with real Developer ID Application and Installer certificates, notarize/staple, pass Gatekeeper, and run a clean download/install test with quarantine preserved. Signing secrets must be configured outside source control.
- Publish a support/contact channel, versioned release notes, supported-DAW/OS matrix, license notices and a privacy statement matching actual behavior. Original-code licensing does not override linked dependencies' terms.

Do not label an installer a production release until the applicable gates have evidence. Do not claim 100% equivalence or invent a feature-completeness percentage.
