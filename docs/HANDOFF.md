# LÖÖP BRÖTHER — handoff

Updated: 2026-10-20 — v0.13.0: the logo is pressed into the bowl in a
deeper biscuit, the ripples swing as long as the rings bob, and resizing
the circle ripples the cells round its edge. v0.12.2 is the last published
release (github.com/minusro/loopbrother/releases; `CHANGELOG.md`).

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
  LÖÖP BRÖTHER logo): every break sits at its
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
  - **Option** (Alt): 8x slower, and the change applies at once instead
    of on the next 16th (`setFineDrag`, then
    `LoopEngine::setMix(..., immediate)`). An Option press leaves the circle
    where it is.
  - **Shift**: sizes the circle like a knob: moving right or up grows its
    radius by as many pixels, left or down shrinks it; the circle stays
    put. With Option as well, an eighth as much.
  - **Command** (Control on Windows): mutes while held, at once both ways
    over the 5 ms ramp (`LoopEngine::setMuted`; the loops run on underneath,
    so unmuting comes back in place), with the clock running or during an
    audition. It only counts with the pointer over the plugin or during a
    pad drag: Command is global on macOS, and Command-clicks in the host
    shouldn't mute it. While it's held the rings under the circle turn dark
    grey and stop following the meter.
- **Logo**: "LÖÖP BRÖTHER" in Cherry Bomb One (`fonts/`, compiled in as
  `jm_assets`, SIL Open Font License in `fonts/OFL-CherryBombOne.txt`, which
  has to ship with the installers), 38 px at 480 to match the old logo's
  ink height, spacing 0. It's pressed into the bowl: filled in
  `jmui::biscuitShade` (#e1d9cf, the bowl's biscuit 10% darker and a touch
  more saturated so it stays warm; until v0.12.2 it took the colour of the
  break under the circle), with a white copy under it, lower by a quarter of the
  thickness of its O's bottom (measured from the font: 1.3 px at 480), so a
  thin white edge shows along the letters' lower sides like a pressed-in
  letter lit from above. Set letter by letter with its baseline on the
  curve concentric with the lip (the lip's ellipse, both semi-axes longer by
  the gap) through the middle letter's baseline, so the lowest point stays
  put, each letter turned to that curve. Drawn as outlines, centred across on its ink and up and down on
  its capitals (the umlauts ride above).
- **The cat**: as the circle's radius passes 60% of its largest, a begging
  calico cat (`images/loops-cat.png`, cut out of `docs/loops cat.jpg` with
  macOS's Vision subject lifting) slides up from the window's bottom edge,
  easing (0.1 s) towards how far the size puts it, up to its middle on the
  edge at the largest (15.47x). Each time it comes up from hidden its
  centre lands somewhere new, at random between 30% and 70% of the width.
  It's drawn last (`paintOverChildren`), 200 px tall at 480, over the
  bowl and the logo; it's only a picture, so presses still reach the pad.
  While the timeline plays it sways 4 degrees left and right about where
  it stands on the edge, like a pendulum (`JungleEditor::catSwayAt`): at
  one side on each beat, lingering there 15% of a beat centred on it, and
  easing out of it, through the middle and into the other side (half a
  cosine) over the rest, so it's moving 85% of the time and never stops
  sharp; upright again over 0.1 s when it stops. (A first version swung
  over in the first 30% of each beat and held, and also squished with the
  output's level; both read as jerky.) The editor reads the
  beat from `uiBeatAt(ms)`: the processor publishes the beat at time zero
  (one atomic, so it never tears) and the tempo, and the editor
  extrapolates to the frame's time, so the sway runs smooth between blocks.
  `jm_snapshot` lets the editor's timer run 40 ms (`JM_CAT_WAIT`) before it
  renders, so a cat that's up shows a live pose. The source photo is 640 px, so at large window
  sizes the cat is upscaled and soft.
- **Window**: the pad over the status bar (44 px at 480, the logo in it),
  resizable by the host window from 240
  to 1440 px wide at that ratio (default 480x524; the
  width is saved in the plugin state and everything scales with it): a
  cream pad (#fffaf6) with white cell borders (2 px at 480), and a status
  bar shaped as the front of a cereal bowl holding the pad: its top edge is
  the bowl's near lip, an arc of an ellipse lowest mid-window and rising
  22 px (at 480) towards the sides, over the pad's lower corners, with a
  white rim and a soft shade along its inner edge (`jmui::bowlLip`, drawn over the pad
  in `paintOverChildren`). Presses under the lip are on the bowl, not the
  pad (`XYPad::hitTest`). The bowl's front is biscuit (#faf4ed, midway
  between the pad's cream and #f5ede4), a shade deeper than the pad so the
  logo's white edge shows, and very slightly deeper towards its sides and
  foot (to #f1eae2, warm) so it reads as round. Every size goes through
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

  No cell borders are drawn. Covered cells take a wash of their break's
  colour by share and carry a cereal ring (a Froot Loop: a thick ring,
  8.5 px radius at 480); every other cell carries a small one at half that
  (4.25 px), drawn from the same sprites. The look is a `jmui::RingStyle`
  (`src/Cereal.{h,cpp}`); `kRingStyles` holds nine to try
  (`docs/ring-styles.png`, from `JM_STYLE_SHEET`), `kRingStyle` is the one
  in use: 8, "Soft clay, no gloss" (deeply tube-shaded, softly lit from
  the top left, a faint coarse grain, a soft shadow, no specular streak). Shadows take a darker tone of the colour turned
  slightly towards red (`shadowOf`), so yellow shades to gold rather than
  olive; the grain is tileable value noise in octaves, so a coarse grain
  stays soft. The marks are pre-rendered (`jmui::CerealSprites`: per
  colour and pattern, at half, full and double size, at the screen's pixel
  density, shadow included) and drawn scaled: with 65 rings a frame paints in
  4 ms at 480 px, 13 at 960, 27 at 1440 (software rendering; it was 18 and
  84 drawing them from scratch). Sprites are drawn at full opacity
  whatever was filled before them (JUCE draws images at the context's
  opacity: the translucent washes faded them once). While a covered break
  plays, its ring follows the break's meter about its centre, from half
  size in silence to double at the loop's peak; when nothing plays it
  rests at full size.
  Dragging pushes the cells the circle's centre actually passes through
  the way the circle goes, and the cells beside the path straight
  outwards, as a wake in liquid: every 60 ms or so the circle leaves a
  push as long as it moved, for the cells along that stretch (sampled
  every 0.004 of the pad, so a fast move catches every cell it crosses)
  and, at 1.5 times that, for the cells around it, falling off with their
  distance from the stretch (Gaussian, 0.1 of the pad; none past 0.2).
  Resizing the circle (Shift) does the same round its edge
  (`XYPad::addSizeDrop`): every 60 ms or so, the cells outside the circle
  within 0.2 of its edge are pushed straight away from its centre by 1.5
  times as far as the edge went out, or towards it as far as it came in,
  falling off with their distance from the edge (the same Gaussian). Each
  push is a damped sine (`rippleSwing`): four half-swings of 0.22 s, out
  with the push, back past where the cell was, out, back, dying away over
  0.45 s, so a cell bobs as if in milk (until v0.12.2 it was a single sin^2
  swell out and back, never past). The cell's small ring moves with it and
  squashes in step, along the push on the swings out and across it on the
  swings back (up to 20% flatter, tanh of the push over 0.006 of the pad),
  the pushes added as vectors at twice their axis's angle so pushes either
  way along an axis add up and a swing across turns the axis a quarter
  (`XYPad::cellBob`); the rings under the circle hold still. A covered cell's wash moves with its
  corners, each the average of the cells meeting there. While the circle
  crosses a cell the pushes add up and it leans forward; once passed, it swings to rest. A cell
  moves at most 0.014 of the pad (7 px at 480); a slow drag moves it 2 to
  3 px. 60 Hz while any run. Drawing only: the sound follows the true
  cells. The meter
  is precomputed per loop at load (RMS with a snappy 1 ms attack and a
  VU-style 300 ms release, run twice round the loop, each 256-frame hop
  keeping its loudest moment so no hit falls between readings, scaled to
  the loop's peak) and read at the playhead the processor publishes; the
  pad repaints at 60 Hz so the snap shows. The selection circle itself is not
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

- **Releases** (`docs/RELEASE.md`): unsigned for now, published as GitHub
  releases by `installer/release.sh`. The Mac installer
  is built here (`installer/macos/make_pkg.sh`: universal, ad-hoc sealed,
  one .pkg with the VST3, AU, app and font licence); Windows by GitHub
  Actions (`.github/workflows/windows.yml`, Inno Setup, VST3 and app),
  which needs the project on GitHub and has never run. Bundle ID
  `com.minus.loopbrother`; the Standalone builds on Windows too.

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
  Option moves an eighth as far, Shift right/up grows the radius a pixel
  per pixel and left/down shrinks it back, Shift+Option an eighth as
  much, the size holds at the smallest and the largest circle, an Option press leaves the circle in place;
  a rightward drag pushes the cells it crossed right (2.5 px), never back,
  still at the end, and a cell well away not at all. Through the
  processor: holding the mute silences it from the next block and letting
  go brings it back, with the clock running and during an audition. Not
  covered: real modifier keys (the Command-over-the-plugin polling), the
  pointer hiding, unbounded movement and the pointer landing on the circle
  at release - those need hands on a mouse.
- The pad renders as intended (`jm_snapshot` renders inspected: the
  largest circle over half the pad, the rings, no borders; close-ups of the
  rings, and of small rings squashed mid-drag; a mid-drag `JM_RIPPLE=1` frame diffed against a still one shows
  marks moved by a pixel or two up to 330 px from the circle); the hidden
  pointer while dragging and the dots pulsing in time have not been seen on
  screen yet. Dense breaks keep the meter between about half and full, so
  their dots swing between roughly 1.25x and 2x; a faster release would
  swing wider.

## Not yet verified

- Painting at 1440 px with the largest circle takes 45 to 50 ms a frame
  in JUCE's software renderer (7 at 480; the drag's wake adds about 3.4 ms
  at 480). On screen macOS draws through Core Graphics, likely faster, but
  it hasn't been timed there. The cost is the area: the cell washes and
  2 px borders scale with the window.

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

- Logo font candidates, for another round: `docs/logo-fonts.png` has 24 on the
  status bar, all free to embed (OFL, Luckiest Guy Apache 2.0), from
  github.com/google/fonts (`JM_LOGO_SHEET=docs/logo-fonts.png
  JM_LOGO_FONTS=<dir of .ttf>`). The shortlist is shown in the whole UI in
  `docs/logo-mockups.png` (`JM_LOGO_FONT=<file> JM_LOGO_KERN=<spacing>` on a
  snapshot): Bagel Fat One and Cherry Bomb One at spacing 0, Sniglet
  Regular at +0.03, and the current one. Bagel and Cherry Bomb draw smaller
  at the same height and want scaling up to match. The chosen font gets
  compiled in (binary data, with its licence file), so it looks the same on
  Windows.

- `docs/TODO.md` holds planned work (the 2026-10-20 list is done).

- Pitch-independent tempo (time-stretch or 16th-note slicing) as an option.
- A choice of quantization (16th / beat / bar / off) once there is UI for it.
- A readout of the selected break (number, bars) once more UI is wanted.
