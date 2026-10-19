#include "LoopLibrary.h"
#include "BreakData.h"

static const char* resourceNamed(const juce::String& file, int& size)
{
    for (int i = 0; i < BreakData::namedResourceListSize; ++i)
        if (file == BreakData::originalFilenames[i])
            return BreakData::getNamedResource(BreakData::namedResourceList[i], size);
    size = 0;
    return nullptr;
}

LoopLibrary::LoopLibrary()
{
    int size = 0;
    const char* json = resourceNamed("layout.json", size);
    const auto layout = juce::JSON::parse(juce::String::fromUTF8(json, size));
    if (const auto* points = layout.getArray())
    {
        juce::WavAudioFormat wav;
        for (const auto& p : *points)
        {
            Entry e;
            e.name = p["file"].toString();
            e.pos  = { (float) (double) p["x"], (float) (double) p["y"] };
            const char* data = resourceNamed(e.name, size);
            if (data == nullptr) continue;
            std::unique_ptr<juce::AudioFormatReader> reader(
                wav.createReaderFor(new juce::MemoryInputStream(data, (size_t) size, false), true));
            if (reader == nullptr || reader->lengthInSamples <= 0) continue;
            const int frames = (int) reader->lengthInSamples;
            e.rate = reader->sampleRate;
            e.audio.setSize(2, frames);
            reader->read(&e.audio, 0, frames, 0, true, true);
            if (reader->numChannels == 1) e.audio.copyFrom(1, 0, e.audio, 0, 0, frames);
            entries.push_back(std::move(e));
        }
    }

    // Each break is a whole number of bars at kLoopBpm.
    std::vector<jm::Pt> sites;
    for (const auto& e : entries)
    {
        const double framesPerBar = e.rate * 240.0 / jm::kLoopBpm;
        const int bars = juce::jmax(1, juce::roundToInt(e.audio.getNumSamples() / framesPerBar));
        loops_.push_back({ e.audio.getReadPointer(0), e.audio.getReadPointer(1), e.audio.getNumSamples(), bars });
        sites.push_back({ e.pos.x, e.pos.y });
    }
    pad_.setSites(sites);
    cells_ = jm::voronoiCells(sites, 0.0, 1.0);
}
