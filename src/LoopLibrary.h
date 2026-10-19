#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_graphics/juce_graphics.h>
#include "engine/LoopEngine.h"
#include "engine/PadGeometry.h"

// The breaks compiled into the plugin (breaks/*.wav) with their pad positions
// (breaks/layout.json), decoded once per process and shared by every instance
// through juce::SharedResourcePointer. Immutable after construction.
class LoopLibrary {
public:
    LoopLibrary();

    struct Entry {
        juce::String name;                 // "042.wav"
        juce::Point<float> pos;            // in [0, 1]^2, y up
        juce::AudioBuffer<float> audio;    // stereo
        double rate = 44100.0;             // the file's sample rate
    };

    int size() const { return (int) entries.size(); }
    const Entry& entry(int i) const { return entries[(size_t) i]; }
    const jm::Loop* loops() const { return loops_.data(); }

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
