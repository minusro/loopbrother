# LÖÖP BRÖTHER — handoff

Updated: 2026-10-19 — v0.8.8, ripples only in the cells the drag crosses.

## Where things stand

LÖÖP BRÖTHER (formerly Jungle Massive) builds VST3 + AU + Standalone, an
instrument with stereo out and MIDI in. Files and plugin lists use the
ASCII name "LOOP BROTHER": the VST3 SDK's moduleinfo.json writer, which
hosts list plugins from, garbles non-ASCII names (each UTF-8 byte
sign-extended into its own character), and JUCE's own VST3 host then
failed to load it. The umlauts are in the logo. The codes (Mnro/Jngl) and
bundle ID are unchanged, so saved sessions find it.

- **Loops**: the 97 breaks in `breaks/` (162 BPM, 1/2/4 bars) and
  `breaks/layout.json` are compiled in as binary data and decoded once per
  process (`LoopLibrary`, shared between instances, ~95 MB of float audio).
  Each is levelled at load to -15 LUFS (`jm::kBreakLufs`). The loudness is
  measured by BS.1770 as the loop sounds repeating
  (`src/engine/Loudness.h`): the filters settle on the loop's end, and the
  400 ms gating blocks wrap round the loop, one every 10 ms rather than the
  standard's 100, so where the blocks fall doesn't matter. Before
  levelling, the breaks spanned 13.7 LU (-20.6 to -6.9 LUFS). The gain is
  applied in float, and 9 breaks now peak above 0 dBFS, the highest at
  +5.5 dBFS. Fine inside a host's float mix, but they could clip the
  standalone's audio device. The WAVs on disk are unchanged.
- **Pad** (the UI, white, over a small white status bar with the
  dark brown LÖÖP BRÖTHER logo): every break sits at its
  similarity-layout position, drawn in by 3% of the pad on every side
  (`jm::padSite`) so the outermost marks have room, and owns the nearest
  patch of pad (drawn as Voronoi cells, reaching the edges). The selection circle is three continuous automatable
  parameters: `x`, `y` and `size` (Circle Size, 0.25x of `jm::kPuckRadius`
  = 12.375/480 of the pad up to 15.47x, the circle that covers half the pad
  from its middle; skewed so 1x sits mid-range). Every break whose cell the
  circle covers plays: the largest circle in the middle plays 74 at once
  (3.3% of a core, against 0.2% for a small circle). Its share is the
  fraction of the circle's area in its cell (`src/engine/PadGeometry.h`:
  exact circle/polygon intersection, shares add up to exactly 1; the mix
  holds up to 128) and its gain is equal power, the square root of the
  share, so a half-and-half blend is as loud as either break alone.
- **Colours**: each break has one of the 7 Froot Loops colours (pastel red,
  orange, yellow, green, blue, purple, pink), from
  `tools/break_analysis/colour_breaks.py` into `breaks/colours.json`. It
  clusters the breaks with k-means in the same feature space as the layout
  (timbre, groove, dynamics), then orders the clusters along the rainbow by
  the shortest path through their centres, so neighbouring colours are
  neighbouring sounds. The clusters are soft (silhouette 0.14; 7 scored
  best in 3 to 8): the breaks are a continuum, and the colours mark regions
  of it. On the pad they form patches: 55% of neighbouring cells share a
  colour, against 19% shuffled.
- **Pad gestures** (`jmui::XYPad`): a press puts the circle there, and
  dragging moves it by as much as the mouse moves (relative, so the
  modifiers below can change speed mid-drag). The pointer is hidden and the
  drag can run past the screen edge (JUCE's unbounded mouse movement). On
  release the pointer reappears on the circle's centre.
  - **Option** (Alt): 16x slower, and the change applies at once instead
    of on the next 16th (`setFineDrag`, then
    `LoopEngine::setMix(..., immediate)`). An Option press leaves the circle
    where it is.
  - **Shift**: the circle grows by as many pixels as the mouse moves away
    from the pivot and shrinks as it comes back. The pivot starts at the
    press point and moves with the circle while Shift is up, so in a plain
    drag it sits under the pointer: a Shift-drag first grows the circle, and
    shrinking below where it started needs an Option move (or a hit on the
    pad edge) to put distance between pointer and pivot first. With Option
    as well, a sixteenth as much.
  - **Command** (Control on Windows): mutes while held, at once both ways
    over the 5 ms ramp (`LoopEngine::setMuted`; the loops run on underneath,
    so unmuting comes back in place), with the clock running or during an
    audition. It only counts with the pointer over the plugin or during a
    pad drag: Command is global on macOS, and Command-clicks in the host
    shouldn't mute it.
- **Window**: the pad over the status bar (44 px at 480, the logo
  "LÖÖP BRÖTHER!" 30 px high in it), resizable by the host window from 240
  to 1440 px wide at that ratio (default 480x524; the
  width is saved in the plugin state and everything scales with it): a
  cream pad (#fffaf6) with white cell borders (2 px at 480), a cream edge
  over the white status bar. Every size goes through
  `jmui::WindowConstrainer`:
  - **Edges:** an edge drag follows that edge.
  - **Corners:** a corner drag follows the corner's further-out side, so
    the pointer stays on the window's edge. It stays a corner drag until
    the sizes stop arriving for 400 ms. A corner dragged off the diagonal
    often moves the mouse along one axis only. Read on its own, such an
    event looks like an edge drag of the other side, and the window flicked
    to that side's size and back.
  - **Standalone:** JUCE draws the frame. A corner drag keeps the opposite
    corner; an edge drag keeps the opposite edge, and the two others move
    half the distance each. `cmake/juce-standalone-resize.patch` stops
    JUCE's standalone wrapper pinning the top-left corner, which made edge
    drags grow right and down.
  - **Plugin hosts:** the host owns the window and only asks for a size.
    `cmake/juce-vst3-resize.patch` makes JUCE's VST3 wrapper ask the
    constrainer, saying which sides changed. JUCE used to fit the ratio to
    the shorter side, which made edges shrink-only. Which edges stay put is
    the host's choice.
  - **Hosts that ignore the constraint:** off-ratio sizes are corrected
    afterwards by the same rules.

  Covered cells take a wash of their break's colour by share and carry an
  Ö drawn as a Froot Loop (a thick ring, 8.5 px radius at 480, with the
  umlaut dots over it); every other cell carries a crumb, a round piece
  the size of one of those dots. The look is a `jmui::RingStyle`
  (`src/Cereal.{h,cpp}`); `kRingStyles` holds nine to try
  (`docs/ring-styles.png`, from `JM_STYLE_SHEET`), `kRingStyle` is the one
  in use: 8, "Soft clay, no gloss" (deeply tube-shaded, softly lit from
  the top left, a faint coarse grain, a soft shadow, no specular streak). Shadows take a darker tone of the colour turned
  slightly towards red (`shadowOf`), so yellow shades to gold rather than
  olive; the grain is tileable value noise in octaves, so a coarse grain
  stays soft. The marks are pre-rendered (`jmui::CerealSprites`: per
  colour and pattern, at half, full and double size, at the screen's pixel
  density, shadow included) and drawn scaled: with 65 Ös a frame paints in
  4 ms at 480 px, 13 at 960, 27 at 1440 (software rendering; it was 18 and
  84 drawing them from scratch). While a covered break plays, its Ö
  follows the break's meter about the ring's centre, from half size in
  silence to double at the loop's peak; when nothing plays it rests at
  full size.
  The Ös' umlauts go round their ring to face the way the circle has
  lately been dragged (the circle's moves averaged over about 0.15 s,
  turning with a 0.08 s lag the short way round), stay there after the
  release, and go back on top after a tap (a press with under 3 px of
  movement). Only the dots move: the O, its shading and every shadow stay
  lit from the top left. `CerealSprites` keeps the O and each dot as
  separate images for this.
  Dragging pushes the cells the circle's centre actually passes through,
  and only those, the way the circle goes, as through liquid: every 60 ms
  or so the circle leaves a push as long as it moved, for the cells along
  that stretch (sampled every 0.004 of the pad, so a fast move catches
  every cell it crosses). Each such cell feels it once, as a single sin^2
  swell out and back to rest (0.22 s, never below zero, so nothing swings
  back and forth): its crumb moves with it (the Ös under the circle hold
  still), and each border corner moves by the average of the cells that
  meet there, so the borders bend without coming apart. While the circle
  crosses a cell it keeps leaning forward; once passed, it settles. A cell
  moves at most 0.014 of the pad (7 px at 480); a slow drag moves it 2 to
  3 px. 60 Hz while any run. Drawing only: the sound follows the true
  cells. The meter
  is precomputed per loop at load (RMS, 10 ms attack, VU-style 300 ms
  release, run twice round the loop, scaled to the loop's peak) and read at
  the playhead the processor publishes. The selection circle itself is not
  drawn - the washed cells show what it covers. Over the pad the pointer
  is the default arrow. (`jmui::HideCursorWhileDragging` hides it on future
  knobs and sliders.)
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

- `jm_tests` (30): song-position sync, mid-bar start, varispeed, stop,
  note-on restart (incl. on the bar line without a doubled downbeat), loop
  switching in phase, host jumps, start after a stopped note-on, no drift
  over 100k odd-sized blocks; mixes (shares, ramps, restart of the whole
  mix); circle/polygon areas; overlap shares (single cell, even split,
  four-way corner, pad edge, random pads against a Monte Carlo count);
  equal-power gains; quantized moves (held until the ramp before the next
  16th, a newer move landing on the same step, a note-on taking the waiting
  mix, the grid following a restart); the timeline (press auditions from
  1.1.1, release stops, transport take-over resyncs, a stop under a held
  pad carries on, free run) and an audition rendered through the engine;
  loudness against EBU Tech 3341's reference signals (steady sines,
  relative and absolute gating, at 48 and 44.1 kHz) and the levelling gain;
  an immediate mix replacing a waiting one; mute and unmute at once,
  mid-step, the loop back in place; a start while muted silent.
- The loop loudness agrees with pyloudnorm, run on each break tiled 32
  times, to within 0.12 LU on all 97 (mean +0.06 LU, the difference between
  its filter and the standard's). The K-weighting reproduces BS.1770's
  48 kHz coefficients exactly.
- `jm_host_check`: the built VST3 hosted with a scripted transport matches
  the break files read from disk and levelled the same way (start at bar 1 through the wrap, a note-on
  mid-block, stop, start mid-song, six puck positions around a cell border
  mixing two breaks at equal power by area, each move held until the next
  16th and complete on it; Circle Size grown to 2.8x covering more cells (4 against 2)
  and shrunk to 0.3x, each change on the 16th at the new area shares; and
  with the transport stopped a held note
  auditioning from 1.1.1 on its sample until its note-off). It caught the pad parameters snapping to 0.01
  steps (JUCE's short AudioParameterFloat constructor), fixed in v0.2.0.
- `auval -v aumu Jngl Mnro` passes.
- Window resizing:
  - The host check resizes the plugin through JUCE's own VST3 host window,
    which asks checkSizeConstraint like a DAW. It opens at 480x504. Edges
    dragged out and in follow the dragged side at the window's ratio.
    Corner drags off the diagonal
    follow the further side, including when the mouse moves along one axis
    only. That case flickered before, and the check fails with the corner
    memory turned off.
  - `JM_RESIZE_CHECK=1 jm_snapshot` checks the standalone frame geometry
    through a bordered constrainer like the standalone's (corner keeps the
    opposite corner; edge keeps the opposite edge, the perpendicular edges
    moving half each way). It also checks the same corner sequences as
    host requests, and the off-ratio fallback.
  - Not yet dragged by hand, in the standalone or a DAW.
- Pad gestures (`JM_PAD_CHECK=1 jm_snapshot`, synthetic mouse events on
  the real editor): a press places the circle, a plain drag follows 1:1,
  Option moves a sixteenth as far, Shift grows the circle away from the
  pivot and shrinks it back, Shift+Option a sixteenth as much, the size
  holds at the largest circle, an Option press leaves the circle in place;
  a rightward drag pushes the cells it crossed right (2.5 px), never back,
  still at the end, and the nearest cell it didn't cross not at all; the umlauts face right after
  it (90 deg), go back on top after a tap (0) and face down after a drag
  down (179, the drag wobbling a pixel), staying put after the release. Through the
  processor: holding the mute silences it from the next block and letting
  go brings it back, with the clock running and during an audition. Not
  covered: real modifier keys (the Command-over-the-plugin polling), the
  pointer hiding, unbounded movement and the pointer landing on the circle
  at release - those need hands on a mouse.
- The pad renders as intended (`jm_snapshot` renders inspected: the
  largest circle over half the pad, crumbs, grey lines; close-ups of the Ö
  marks; a mid-drag `JM_RIPPLE=1` frame diffed against a still one shows
  crumbs moved by a pixel or two up to 330 px from the circle); the hidden
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

- Pick the logo font from `docs/logo-fonts.png`: 23 candidates, all free to
  embed (OFL, Luckiest Guy Apache 2.0), downloaded from github.com/google/
  fonts; render it again with `JM_LOGO_SHEET=docs/logo-fonts.png
  JM_LOGO_FONTS=<dir of .ttf>`. The chosen one gets compiled in (binary
  data, with its licence file), so it looks the same on Windows.

- `docs/TODO.md` holds planned work (the 2026-10-20 list is done).

- Pitch-independent tempo (time-stretch or 16th-note slicing) as an option.
- A choice of quantization (16th / beat / bar / off) once there is UI for it.
- A readout of the selected break (number, bars) once more UI is wanted.
