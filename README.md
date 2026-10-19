# Jungle Massive

A break-loop instrument plugin (VST3 / AU / Standalone, JUCE). 97 drum breaks,
tuned to 162 BPM and laid out by similarity on an XY pad: move the puck to
switch breaks. Loops follow the host tempo (varispeed) and transport, and any
note-on restarts the loop from its first beat.

State of the code: `docs/HANDOFF.md`. Developer guide: `CLAUDE.md`.

```sh
cmake -B build && cmake --build build --parallel 8   # plugins + standalone
g++ -std=c++17 -O2 -I src tests/engine_tests.cpp -o /tmp/t && /tmp/t  # engine tests
```

`tools/break_analysis/` holds the scripts that cropped, tuned and laid out the
breaks in `breaks/`.
