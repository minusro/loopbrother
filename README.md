# Jungle Massive

A break-loop instrument plugin (VST3 / AU / Standalone, JUCE). 97 drum breaks,
tuned to 162 BPM, levelled to the same loudness (-15 LUFS) and laid out by
similarity on an XY pad: the selection circle plays every break whose cell it
covers, blended at equal power by the area it covers. Loops follow the host tempo (varispeed) and transport, and any note-on
restarts them from their first beat. With the transport stopped, holding the
pad or a note auditions the loops from their first beat.

State of the code: `docs/HANDOFF.md`. Developer guide: `CLAUDE.md`.

```sh
cmake -B build && cmake --build build --parallel 8   # plugins + standalone
g++ -std=c++17 -O2 -I src tests/engine_tests.cpp -o /tmp/t && /tmp/t  # engine tests
```

`tools/break_analysis/` holds the scripts that cropped, tuned and laid out the
breaks in `breaks/`.
