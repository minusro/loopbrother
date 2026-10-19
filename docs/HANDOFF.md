# Jungle Massive — handoff

Updated: 2026-10-19 — v0.2.1, equal-power area mixing, yellow and white UI.

## Where things stand

Jungle Massive builds VST3 + AU + Standalone ("Jungle Massive", Mnro/Jngl),
an instrument with stereo out and MIDI in.

- **Loops**: the 97 breaks in `breaks/` (162 BPM, 1/2/4 bars) and
  `breaks/layout.json` are compiled in as binary data and decoded once per
  process (`LoopLibrary`, shared between instances, ~95 MB of float audio).
- **Pad** (the only UI): every break sits at its similarity-layout position
  and owns the nearest patch of pad (drawn as Voronoi cells). The selection
  circle (radius `jm::kPuckRadius` = 16.5/480 of the pad) is two continuous
  automatable parameters, `x` and `y`. Every break whose cell the circle
  covers plays. Its share is the fraction of the circle's area in its cell
  (`src/engine/PadGeometry.h`: exact circle/polygon intersection, shares add
  up to exactly 1; at most 8 breaks) and its gain is equal power, the square
  root of the share, so a half-and-half blend is as loud as either break
  alone. The pad is yellow in a white frame: covered cells whiten by share,
  each sounding break shows a white ring tracing its playhead, and the
  selection circle is white. The mouse pointer is hidden while dragging
  (`jmui::HideCursorWhileDragging`, meant for every future knob and slider).
- **Playback** (`src/engine/LoopEngine.h`): the mix, every loop varispeed at
  the host tempo and locked to the same 1.1.1. Transport start syncs to the
  song position (loop 1.1.1 on beat 0, so a 2-bar loop starts over on bars 1,
  3, 5 ...). Transport stop fades out and goes silent. A note-on (any note, any channel) restarts the
  mix at 1.1.1 on that sample; while the transport is stopped it is ignored.
  Moving the circle keeps the position in the bar. Mix changes, restarts,
  host jumps and start/stop all ramp over 5 ms; a jump that lands on the same
  spot in a loop (a loop region that is a whole number of loops) is seamless.
- **Standalone**: no transport there, so it free-runs at 162 BPM.

## Verified

- `jm_tests` (18): song-position sync, mid-bar start, varispeed, stop,
  note-on restart (incl. on the bar line without a doubled downbeat), loop
  switching in phase, host jumps, start after a stopped note-on, no drift
  over 100k odd-sized blocks; mixes (shares, ramps, restart of the whole
  mix); circle/polygon areas; overlap shares (single cell, even split,
  four-way corner, pad edge, random pads against a Monte Carlo count);
  equal-power gains.
- `jm_host_check`: the built VST3 hosted with a scripted transport matches
  the break files read from disk (start at bar 1 through the wrap, a note-on
  mid-block, stop, start mid-song, six puck positions around a cell border
  mixing two breaks at equal power by area). It caught the pad parameters snapping to 0.01
  steps (JUCE's short AudioParameterFloat constructor), fixed in v0.2.0.
- `auval -v aumu Jngl Mnro` passes.
- The pad renders as intended (`jm_snapshot` render inspected); the hidden
  pointer while dragging has not been seen on screen yet.

## Not yet verified

- No DAW testing: Ableton/Logic/Bitwig transport, loop regions, tempo
  automation, state recall, many instances.
- Interpolation is 4-point Catmull-Rom with no anti-aliasing filter: fine near
  162 BPM, may alias a little far above it (e.g. 200+ BPM).

## Next ideas

- Pitch-independent tempo (time-stretch or 16th-note slicing) as an option.
- Quantized switching (next 16th / beat) instead of immediate.
- A readout of the selected break (number, bars) once more UI is wanted.
