# Jungle Massive — to do

## Tomorrow (2026-10-20)

"1/4 crossing" below means the quarter-beat (16th) grid that pad moves
already wait for (`LoopEngine::kStepBeats`).

- [ ] **Fine drag.** Dragging while holding Command (Control on Windows)
  moves the selection 10 times slower, and the changes take effect
  immediately instead of waiting for the next 1/4 crossing.
- [ ] **Radius drag.** Dragging while holding Shift grows the selection
  circle as the mouse moves away from the pivot and shrinks it as the mouse
  moves towards it. The pivot starts at the point where the press began,
  and follows the selection while Shift isn't held.
  - The radius is fixed today (`jm::kPuckRadius`). Making it adjustable
    means a new parameter, which adds a parameter ID to the saved state.
- [ ] **Mute.** Pressing the pad while holding Option (Alt on Windows)
  with the clock running mutes the instrument, and the next such press
  unmutes it. Muting and unmuting both wait for the next 1/4 crossing.
  - Open question: is mute a toggle per press (as above), or muted only
    while the press is held?
- [ ] **White dots when idle.** The dots are white when nothing plays: the
  clock is stopped, no MIDI note is held and the pad isn't held.
- [ ] **Brighter green when playing.** While playing, the dots are midway
  between the current green (#2E7D32) and pure green (#00FF00), which works
  out to about #17BE19.
