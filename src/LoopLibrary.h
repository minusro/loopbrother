#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_graphics/juce_graphics.h>
#include "engine/LoopEngine.h"

// The breaks compiled into the plugin (breaks/*.wav) with their pad positions
// (breaks/layout.json), decoded once per process and shared by every instance
// through juce::SharedResourcePointer.
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

    // The loop whose pad position is closest to p: the pad is a Voronoi map.
    int nearest(juce::Point<float> p) const;

private:
    std::vector<Entry> entries;
    std::vector<jm::Loop> loops_;
};
