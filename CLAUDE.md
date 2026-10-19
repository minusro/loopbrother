# Jungle Massive — Claude Code guide

Break-loop instrument plugin (JUCE; VST3 + AU + Standalone on macOS). The 97
breaks in `breaks/` are compiled into the binary; an XY pad's selection circle
mixes the breaks whose cells it covers, in sync with the host. Start with
`docs/HANDOFF.md`.

## Ground rules

- The engine in `src/engine/` is plain C++17 with **no JUCE dependency**. Keep
  it that way: `tests/engine_tests.cpp` compiles against it with bare g++.
- Timing is in beats (quarter notes), absolute from the host transport. A
  loop's read position is a pure function of `beat - anchor`, never a sample
  counter, so it stays phase-locked through tempo changes and jumps.
- Loops are varispeed: every break is tuned to 162 BPM (`jm::kLoopBpm`) and
  always spans its bars in host beats, so pitch follows the host tempo.
- Anything that changes what sounds (mix, restart, jump, start/stop)
  ramps over 5 ms. Don't introduce hard cuts.
- The timeline comes from `jm::TransportFollower` (`src/engine/Transport.h`):
  the host transport while it plays, an audition from 1.1.1 while the pad or
  a MIDI note is held with the transport stopped, a free run with no
  transport at all. The processor renders in segments between note events,
  so auditions start and stop on the note's sample.
- Every break plays at the same loudness, `jm::kBreakLufs` (-15 LUFS,
  BS.1770 measured round the loop, `src/engine/Loudness.h`). `LoopLibrary`
  applies the gain at load, in float, so loud-peaked breaks may peak above
  0 dBFS; the WAVs in `breaks/` stay as they are. The host check levels its
  reference audio the same way.
- Pad moves are quantized: while playing, a new mix waits for the next 16th
  on the loops' grid (counted from 1.1.1, `LoopEngine::kStepBeats`), its
  ramp ending on the step. Restarts, jumps and transport starts are
  immediate and take any waiting mix with them.
- The mix is geometric: each break's share is the fraction of the selection
  circle (`jm::kPuckRadius`) inside its Voronoi cell, so the shares add up to
  exactly 1 (`src/engine/PadGeometry.h`). Gains are equal power, the square
  root of the share (`jm::equalPower`), so the squared gains add up to 1.
- The editor palette is yellow and white (`jmui::yellow`, `white`, `shade`);
  new controls draw from it.
- Every draggable control (the pad now, knobs and sliders later) hides the
  mouse pointer while dragged: give it a `jmui::HideCursorWhileDragging` via
  `addMouseListener`.
- Continuous parameters need an explicit `NormalisableRange`: the short
  `AudioParameterFloat(id, name, min, max, def)` constructor snaps to 0.01.
- JUCE is cloned into the build tree and patched (`cmake/*.patch`, applied by
  FetchContent's PATCH_COMMAND after resetting the clone). Both patches hand
  window sizing to the editor's `jmui::SquareConstrainer`:
  `juce-vst3-resize.patch` makes JUCE's VST3 wrapper ask it, saying which
  sides the host changed, and `juce-standalone-resize.patch` keeps the
  standalone window where it places it. The constrainer keeps a corner drag
  a corner drag until the drag ends, so an off-diagonal drag can't flicker.
  Keep patches minimal and say why in the patch; a new JUCE version means
  re-checking each one. Regenerate a patch with `git diff` in
  `build/_deps/juce-src`; the files there are CRLF and so are the patches.
- No allocation, locking or logging in `processBlock`.
- Parameter IDs (`x`, `y`) are part of saved state. Add new ones, never rename.
- An engine change is not done until `jm_tests` passes; add a check when you
  add behaviour. Plumbing changes (transport, MIDI, loading) should also pass
  the host check below.
- The breaks and their pad layout come from `tools/break_analysis/` (crop,
  tune to 162 BPM, similarity layout). Regenerate `breaks/layout.json` with
  `layout_breaks.py` whenever the loops change.
- Commit every change (see the user's standing request); commit messages:
  imperative subject, body explains the musical/design reasoning.
- Versioning: from `project(... VERSION)`; patch for small fixes, minor for a
  batch of real changes, major for paradigm steps. Started at 0.1.0.

## Quick commands

```sh
# Engine tests, no JUCE needed
g++ -std=c++17 -O2 -I src tests/engine_tests.cpp -o /tmp/jm_tests && /tmp/jm_tests

# Dev build (arm64 only, faster) - plugins install to ~/Library/Audio/Plug-Ins.
# JM_JUCE_DIR can point at a local JUCE git checkout to clone 8.0.4 from
# instead of GitHub (e.g. ../canon/build/_deps/juce-src); it is never modified.
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build --parallel 8

# End-to-end: host the built VST3 with a scripted transport + MIDI
cmake -B build -DJM_BUILD_HOST_CHECK=ON && cmake --build build --target jm_host_check
build/jm_host_check_artefacts/Release/jm_host_check \
    "build/junglemassive_artefacts/Release/VST3/Jungle Massive.vst3" breaks

# Render the editor to a PNG (no window, no audio device) to check the UI
cmake -B build -DJM_BUILD_SNAPSHOT=ON && cmake --build build --target jm_snapshot
build/jm_snapshot_artefacts/Release/jm_snapshot /tmp/jm.png 0.5 0.5 [size]
JM_RESIZE_CHECK=1 build/jm_snapshot_artefacts/Release/jm_snapshot /tmp/jm.png   # window frame, edge + corner drags

# AU validation
auval -v aumu Jngl Mnro
```
