# LÖÖP BRÖTHER — to do

## Tomorrow (2026-10-19)

"1/4 crossing" below means the quarter-beat (16th) grid that pad moves
already wait for (`LoopEngine::kStepBeats`).

- [x] **Fine drag.** Dragging while holding Command (Control on Windows)
  moves the selection 10 times slower, and the changes take effect
  immediately instead of waiting for the next 1/4 crossing.
- [x] **Radius drag.** Dragging while holding Shift grows the selection
  circle as the mouse moves away from the pivot and shrinks it as the mouse
  moves towards it. The pivot starts at the point where the press began,
  and follows the selection while Shift isn't held.
  - Done as a new parameter, Circle Size (`size`, 0.25x to 4x). Because the
    pivot sits under the pointer in a plain drag, a Shift-drag can only
    grow the circle at first. To shrink below the starting size, a Command
    move first has to put distance between pointer and pivot.
- [x] **Mute.** With the clock running, the instrument is muted only while
  Option (Alt on Windows) is held, never as a
  toggle. Letting go unmutes it. Muting and unmuting both wait
  for the next 1/4 crossing.
  - Option counts only with the pointer over the plugin (or during a pad
    drag). macOS reports Option globally, so otherwise Option-clicks in
    the DAW would mute it whenever its window is open.
- [x] **White dots when idle.** The dots are white when nothing plays: the
  clock is stopped, no MIDI note is held and the pad isn't held.
- [x] **Brighter green when playing.** While playing, the dots are midway
  between the current green (#2E7D32) and pure green (#00FF00), which works
  out to about #17BE19.
- [x] **Rename app.** The app name is now LÖÖP BRÖTHER (no accents in the file names but add a small title bar on top with an accented logo)
  - The name hosts list is ASCII too, LOOP BROTHER: the VST3 SDK's
    moduleinfo.json writer garbles umlauts (LÖÖP became LￃﾖￃﾖP), and hosts
    list plugins from that file. The umlauts are in the logo.