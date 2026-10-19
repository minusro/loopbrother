#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_graphics/juce_graphics.h>
#include "engine/LoopEngine.h"
#include "engine/PadGeometry.h"

// The breaks compiled into the plugin (breaks/*.wav) with their pad positions
// (breaks/layout.json), decoded once per process, levelled to jm::kBreakLufs
// and shared by every instance through juce::SharedResourcePointer. Immutable
// after construction.
class LoopLibrary {
public:
    LoopLibrary();

    struct Entry {
        juce::String name;                 // "042.wav"
        juce::Point<float> pos;            // on the pad, [0, 1]^2, y up (jm::padSite of the layout)
        juce::AudioBuffer<float> audio;    // stereo, levelled
        double rate = 44100.0;             // the file's sample rate
        std::vector<float> meter;          // level through the loop, 0..1 of its peak
        int colour = 0;                    // similarity cluster, in rainbow order (breaks/colours.json)
    };

    int size() const { return (int) entries.size(); }
    const Entry& entry(int i) const { return entries[(size_t) i]; }
    const jm::Loop* loops() const { return loops_.data(); }

    // The loop's meter reading at a position through it (0..1), relative to
    // the loop's own peak: 0 in silence, 1 at its loudest moment.
    float level(int loop, double phase) const;

    // Which breaks the selection circle covers, and how much of each.
    const jm::PadMap& pad() const { return pad_; }
    // Each break's cell clipped to the pad, for drawing.
    const std::vector<jm::Poly>& cells() const { return cells_; }

private:
    std::vector<Entry> entries;
    std::vector<jm::Loop> loops_;
    jm::PadMap pad_;
    std::vector<jm::Poly> cells_;
};
