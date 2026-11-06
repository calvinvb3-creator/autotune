# Mac / Logic acceptance gate — pending

Do not mark the product Logic-compatible until this gate has been run on an
Apple silicon Mac. The scripts and tests are delivered, not proof they passed.

Record: Mac model/chip, macOS version, Logic version, audio interface,
sample rate, I/O buffer, build revision, test date, and results.

## 1. Build and automated processor tests

- Run `bash scripts/build-mac.sh`. All three CTest suites and auval must pass.
- The host harness exercises real `PitchProcessor` objects at 44.1, 48, and
  96 kHz, mono/stereo, with block sizes 32, 64, 127, 256, 511, and 1024.
- It verifies automatic tuning, capture, manual playback correction,
  save/restore into another processor, delayed dry bypass, invalid state
  input, buffer counters, and editor construction.
- A passing harness is still not an actual Logic playback test.

## 2. AU registration and validation

- Quit Logic. Build a package with `python3 scripts/package_macos.py` after the
  build passes, then install the generated `.pkg`. Exercise the normal wizard
  once with AU only and once with both AU and VST3. Confirm the standard system
  paths, package version, component choices and administrator prompt.
- Verify an upgrade replaces the old bundle. Check that there are no duplicate
  user-level prototype components shadowing the installed system copy.
- `build/validation/auval.txt` must contain `AU VALIDATION SUCCEEDED`.
- Restart Logic; verify the effect appears in Plug-in Manager and on a mono
  vocal channel. Repeat on a stereo channel.
- Load the installed VST3 in a compatible host and confirm render, state and
  editor behavior after installation, beyond the build-time binary harness.
- If a build, test, or validator fails, preserve its log. Do not bypass the
  failing gate or label the binary validated.

## 3. Automatic correction

- Use a sustained vocal or a generated test tone approximately 35 cents flat
  of A4. With Chromatic and 100% correction, the settled output should be near
  A4. Compare with an independent tuner or spectrum analyzer after the plug-in.
- Change key and scale and verify excluded notes are not chosen.
- Compare 0 ms, 65 ms, and 200 ms retune time; transitions must change audibly.
- Set Correction to 0%; compare against bypass. After its short fade, this
  should be the delayed original signal.
- Test low/high notes, slides, vibrato, breath, consonants, and silence. Listen
  for octave errors, chirps, warbling, formant damage, clicks, and dropouts.
- Repeat with a real recorded vocal. Synthetic-tone success alone does not
  establish natural vocal sound quality.

## 4. Manual editor

- On a dry vocal, Capture notes, play a single pass, then Stop capture.
- Confirm timeline positions agree with the source and the take is intact.
- Drag one sustained note up three semitones. Replay the same source region;
  verify the output pitch changes while the other notes remain as intended.
- Shift-drag a note by a few cents, use arrow keys, undo a move, and reset a
  note with double-click. Compare with an independent tuner.
- Stop/start at several positions, seek backwards, and loop a region after
  capture. Check timing and absence of stale audio.
- Capture is single-pass. Looping while capture is armed is not supported.
- Save the Logic project, quit Logic, reopen, and confirm note edits, key,
  scale, retune time, correction amount, and mode return correctly.
- Duplicate the channel: each instance's edits must remain independent.
- Close/reopen and resize the editor; test grid scroll/zoom and keyboard focus.
- Move the source region only after clearing and recapturing. Automatic note
  relocation with region or tempo changes is not implemented.

## 5. Timing and rendering

- Use a transient test to compare corrected and bypassed tracks. The plug-in
  reports approximately 125–128 ms; actual compensated playback must align.
- Test Logic delay compensation settings and Low Latency Mode separately.
  Live monitoring is expected to retain substantial latency in this version.
- Compare realtime playback, realtime bounce, and offline bounce. Inspect the
  start and end of the vocal for lost samples, silence, or truncated tails.
- Run at 44.1/48/96 kHz and I/O buffers 32/64/128/256/512/1024 where supported.
- Test multiple instances, a long session, and sample-rate changes. No
  overloads, rising buffer-error count, crashes, or corrupted state are allowed.
- Test host bypass and the plug-in bypass button. Neither should shift timing
  or produce a click after the transition.

## Release decision

Any failed audio, timing, state, or validator check blocks a functional release.
Document failures with input audio, reproduction steps, expected/actual
behavior, and logs. “Pass” requires observed results, not a code review.
