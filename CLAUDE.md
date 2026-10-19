# Jungle Massive — Claude Code guide

Break-loop instrument plugin (JUCE; VST3 + AU + Standalone on macOS). The 97
breaks in `breaks/` are compiled into the binary; an XY pad picks which one
plays, in sync with the host. Start with `docs/HANDOFF.md`.

## Ground rules

- The engine in `src/engine/` is plain C++17 with **no JUCE dependency**. Keep
  it that way: `tests/engine_tests.cpp` compiles against it with bare g++.
- Timing is in beats (quarter notes), absolute from the host transport. A
  loop's read position is a pure function of `beat - anchor`, never a sample
  counter, so it stays phase-locked through tempo changes and jumps.
- Loops are varispeed: every break is tuned to 162 BPM (`jm::kLoopBpm`) and
  always spans its bars in host beats, so pitch follows the host tempo.
- Anything that changes what sounds (switch, restart, jump, start/stop)
  crossfades over 5 ms. Don't introduce hard cuts.
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
# JM_JUCE_DIR can point at a local JUCE 8.0.4 checkout to skip the fetch.
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build --parallel 8

# End-to-end: host the built VST3 with a scripted transport + MIDI
cmake -B build -DJM_BUILD_HOST_CHECK=ON && cmake --build build --target jm_host_check
build/jm_host_check_artefacts/Release/jm_host_check \
    "build/junglemassive_artefacts/Release/VST3/Jungle Massive.vst3" breaks

# AU validation
auval -v aumu Jngl Mnro
```
