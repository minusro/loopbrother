# Jungle Massive — handoff

Updated: 2026-10-19 — v0.1.0, first playable build.

## Where things stand

Jungle Massive builds VST3 + AU + Standalone ("Jungle Massive", Mnro/Jngl),
an instrument with stereo out and MIDI in.

- **Loops**: the 97 breaks in `breaks/` (162 BPM, 1/2/4 bars) and
  `breaks/layout.json` are compiled in as binary data and decoded once per
  process (`LoopLibrary`, shared between instances, ~95 MB of float audio).
- **Pad** (the only UI): every break sits at its similarity-layout position
  and owns the nearest patch of pad (drawn as Voronoi cells). The puck is two
  automatable parameters, `x` and `y`; the break nearest the puck is the one
  that plays. The sounding break shows a ring tracing its playhead.
- **Playback** (`src/engine/LoopEngine.h`): one loop at a time, varispeed at
  the host tempo. Transport start syncs to the song position (loop 1.1.1 on
  beat 0, so a 2-bar loop starts over on bars 1, 3, 5 ...). Transport stop
  fades out and goes silent. A note-on (any note, any channel) restarts the
  loop at 1.1.1 on that sample; while the transport is stopped it is ignored.
  Switching loops keeps the position in the bar. Switches, restarts, host
  jumps and start/stop all crossfade over 5 ms; a jump that lands on the same
  spot in the loop (a loop region that is a whole number of loops) is seamless.
- **Standalone**: no transport there, so it free-runs at 162 BPM.

## Verified

- `jm_tests` (11): song-position sync, mid-bar start, varispeed, stop,
  note-on restart (incl. on the bar line without a doubled downbeat), loop
  switching in phase, host jumps, start after a stopped note-on, no drift
  over 100k odd-sized blocks.
- `jm_host_check`: the built VST3 hosted with a scripted transport matches
  the break file read from disk (start at bar 1 through the wrap, a note-on
  mid-block, stop, start mid-song).
- `auval -v aumu Jngl Mnro` passes.
- The pad renders as intended in the standalone (screenshot inspection).

## Not yet verified

- No DAW testing: Ableton/Logic/Bitwig transport, loop regions, tempo
  automation, state recall, many instances.
- Interpolation is 4-point Catmull-Rom with no anti-aliasing filter: fine near
  162 BPM, may alias a little far above it (e.g. 200+ BPM).

## Next ideas

- Pitch-independent tempo (time-stretch or 16th-note slicing) as an option.
- Quantized switching (next 16th / beat) instead of immediate.
- A readout of the selected break (number, bars) once more UI is wanted.
