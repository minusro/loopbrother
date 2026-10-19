# Jungle Massive — handoff

Updated: 2026-10-19 — v0.4.3, natural window resizing (JUCE VST3 patch).

## Where things stand

Jungle Massive builds VST3 + AU + Standalone ("Jungle Massive", Mnro/Jngl),
an instrument with stereo out and MIDI in.

- **Loops**: the 97 breaks in `breaks/` (162 BPM, 1/2/4 bars) and
  `breaks/layout.json` are compiled in as binary data and decoded once per
  process (`LoopLibrary`, shared between instances, ~95 MB of float audio).
- **Pad** (the only UI): every break sits at its similarity-layout position
  and owns the nearest patch of pad (drawn as Voronoi cells). The selection
  circle (radius `jm::kPuckRadius` = 12.375/480 of the pad) is two continuous
  automatable parameters, `x` and `y`. Every break whose cell the circle
  covers plays. Its share is the fraction of the circle's area in its cell
  (`src/engine/PadGeometry.h`: exact circle/polygon intersection, shares add
  up to exactly 1; at most 8 breaks) and its gain is equal power, the square
  root of the share, so a half-and-half blend is as loud as either break
  alone. The pad fills the window, square and resizable by the host window
  from 240 to 1440 px (default 960; the size is saved in the plugin state
  and everything on the pad scales with it), yellow with deeper-yellow cell
  borders. The 1:1 ratio is kept by JUCE's constrainer. Where JUCE owns the
  window (the standalone) dragging a corner keeps the opposite corner and
  dragging an edge keeps the opposite edge while the two others move half
  the distance each. In plugin hosts the host owns the window and only asks
  for a size: JUCE's VST3 answer is patched (`cmake/juce-vst3-resize.patch`)
  to follow the dragged side instead of the shorter one (which made edges
  shrink-only); which edges stay put is then the host's choice. Sizes that
  still arrive non-square (a host ignoring the constraint) are squared
  afterwards along the side that moved. Covered cells whiten by share and only they carry a dot;
  while a covered break plays, its dot follows the break's meter, from half
  its size (6 px radius at 480) in silence to double at the loop's peak. The meter
  is precomputed per loop at load (RMS, 10 ms attack, VU-style 300 ms
  release, run twice round the loop, scaled to the loop's peak) and read at
  the playhead the processor publishes. The selection circle itself is not
  drawn - the whitened cells show what it covers. The mouse pointer is
  hidden while dragging
  (`jmui::HideCursorWhileDragging`, meant for every future knob and slider).
- **Playback** (`src/engine/LoopEngine.h`): the mix, every loop varispeed at
  the host tempo and locked to the same 1.1.1. Transport start syncs to the
  song position (loop 1.1.1 on beat 0, so a 2-bar loop starts over on bars 1,
  3, 5 ...). Transport stop fades out and goes silent. A note-on (any note, any channel) restarts the
  mix at 1.1.1 on that sample; while the transport is stopped it is ignored.
  Moving the circle keeps the position in the bar, and the move is quantized:
  the new mix waits for the next 16th on the loops' grid (counted from
  1.1.1, so it follows note-on restarts), its 5 ms ramp ending on the step; a
  newer move before then replaces the waiting one on the same step, and a
  restart, jump or transport start takes it at once. Restarts, host jumps
  and start/stop ramp over 5 ms too; a jump that lands on the same spot in a
  loop (a loop region that is a whole number of loops) is seamless. The pad
  shows the circle's new position at once, ahead of the audio.
- **Audition**: with the host transport stopped, holding the pad or any MIDI
  note plays the mix from 1.1.1 at the host tempo until the last hold ends
  (a pad press starts on the pressed spot; a note starts on its exact
  sample). Further note-ons while auditioning restart at 1.1.1 as usual. If
  the transport starts under a hold it takes over on the song position; if
  it stops under a hold the audition carries on seamlessly. The editor
  reports the press (`setPadHeld`, cleared when the editor closes), the
  processor tracks held notes per channel (all-notes-off / all-sound-off
  clear them) and renders in segments between note events;
  `jm::TransportFollower` picks the timeline for each segment.
- **Standalone**: no transport there, so it free-runs at 162 BPM.

## Verified

- `jm_tests` (25): song-position sync, mid-bar start, varispeed, stop,
  note-on restart (incl. on the bar line without a doubled downbeat), loop
  switching in phase, host jumps, start after a stopped note-on, no drift
  over 100k odd-sized blocks; mixes (shares, ramps, restart of the whole
  mix); circle/polygon areas; overlap shares (single cell, even split,
  four-way corner, pad edge, random pads against a Monte Carlo count);
  equal-power gains; quantized moves (held until the ramp before the next
  16th, a newer move landing on the same step, a note-on taking the waiting
  mix, the grid following a restart); the timeline (press auditions from
  1.1.1, release stops, transport take-over resyncs, a stop under a held
  pad carries on, free run) and an audition rendered through the engine.
- `jm_host_check`: the built VST3 hosted with a scripted transport matches
  the break files read from disk (start at bar 1 through the wrap, a note-on
  mid-block, stop, start mid-song, six puck positions around a cell border
  mixing two breaks at equal power by area, each move held until the next
  16th and complete on it, and with the transport stopped a held note
  auditioning from 1.1.1 on its sample until its note-off). It caught the pad parameters snapping to 0.01
  steps (JUCE's short AudioParameterFloat constructor), fixed in v0.2.0.
- `auval -v aumu Jngl Mnro` passes.
- Window resizing: the host check resizes the plugin through JUCE's own
  VST3 host window (which asks checkSizeConstraint like a DAW) - edges out
  and in follow the dragged side and stay square; `JM_RESIZE_CHECK=1
  jm_snapshot` checks the standalone frame geometry (corner keeps the
  opposite corner, edge keeps the opposite edge, perpendicular edges half
  each way) and the non-square fallback. Not yet dragged in a real DAW.
- The pad renders as intended (`jm_snapshot` render inspected); the hidden
  pointer while dragging and the dots pulsing in time have not been seen on
  screen yet. Dense breaks keep the meter between about half and full, so
  their dots swing between roughly 1.25x and 2x; a faster release would
  swing wider.

## Not yet verified

- No DAW testing: Ableton/Logic/Bitwig transport, loop regions, tempo
  automation, state recall, many instances, window resizing (it relies on
  the host letting the plugin window be dragged; there is no corner grip).
  Auditioning needs the host to keep processing the instrument while
  stopped; some hosts (Logic with the track not selected) put idle
  instruments to sleep. The pad press itself is not covered by the host
  check (it comes from the editor); held notes are.
- Interpolation is 4-point Catmull-Rom with no anti-aliasing filter: fine near
  162 BPM, may alias a little far above it (e.g. 200+ BPM).

## Next ideas

- Pitch-independent tempo (time-stretch or 16th-note slicing) as an option.
- A choice of quantization (16th / beat / bar / off) once there is UI for it.
- A readout of the selected break (number, bars) once more UI is wanted.
