# Changelog

What changed for players in each release of LÖÖP BRÖTHER. The installers
are on the [Releases](https://github.com/minusro/loopbrother/releases) page.

## Unreleased

- The logo is pressed into the bowl: a slightly darker shade of the bowl,
  with its white edge below. It no longer takes the colour of the break
  under the circle.
- The ripples last as long as the rings' bobbing: a pushed cell swings
  back past where it was and out again a few times, dying away, instead
  of settling after one push.
- Growing or shrinking the circle (Shift-drag) ripples the cells round
  its edge outwards or inwards, bobbing the same way.

## v0.12.2 — 2026-10-20

Everything since v0.9.1.

### Playing

- Fine drags (Option, Alt on Windows) move the circle at 1/8 speed, up
  from 1/16.
- The bowl's rim now covers the pad's lower corners, so a press there
  doesn't move the circle.

### Look

- The status bar is now the front of a cereal bowl, its rim arcing over
  the pad's lower corners.
- The logo follows the curve of the rim, pressed in with a white edge
  below it. When the break under the circle changes, its colour fades
  round the colour wheel in a tenth of a second and stays a pastel all the
  way.
- Every break shows as a plain cereal ring: full size under the circle,
  pulsing with its break while it plays, and half size everywhere else.
  The cell borders and the rings' umlauts are gone.
- Dragging leaves a wider wake. Besides the cells the circle crosses, the
  cells beside its path are pushed outwards, and the small rings bob as
  they settle, wobbling a few times.
- While Command (Control on Windows) mutes, the rings under the circle
  turn dark grey and hold still.

### The cat

- Make the circle nearly as big as it goes and a cat comes up from the
  bottom of the window, at a different spot each time. It sways to the
  beat while the loops play.

## v0.9.1 — 2026-10-19

The first release: VST3, Audio Unit and standalone app for macOS
(universal), VST3 and app for Windows (64-bit). Unsigned.

- 97 drum breaks, tuned to 162 BPM, follow the host's tempo (varispeed).
  They're laid out on an XY pad by how alike they sound.
- The selection circle mixes every break it covers, each by its share of
  the circle, at equal power. Every break plays at the same loudness
  (-15 LUFS).
- While playing, a move lands on the next 16th. A MIDI note restarts the
  loops from the top. With the transport stopped, holding the pad or a
  note auditions from there.
- Shift-drag sizes the circle like a knob, Option (Alt) drags finely, and
  Command (Control) mutes while held over the plugin.
- The window resizes from any edge or corner and keeps its shape.
