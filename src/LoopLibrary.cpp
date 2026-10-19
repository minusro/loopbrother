#include "LoopLibrary.h"
#include "BreakData.h"
#include "engine/Loudness.h"
#include <map>

static const char* resourceNamed(const juce::String& file, int& size)
{
    for (int i = 0; i < BreakData::namedResourceListSize; ++i)
        if (file == BreakData::originalFilenames[i])
            return BreakData::getNamedResource(BreakData::namedResourceList[i], size);
    size = 0;
    return nullptr;
}

// One meter value per this many frames (about 6 ms at 44.1 kHz).
constexpr int kMeterHop = 256;

// The level a meter shows through the loop: RMS with a 10 ms attack and a
// VU-style 300 ms release, run twice round the loop so the start already
// carries the end's decay, then scaled so the loop's peak reads 1.
static std::vector<float> meterOf(const juce::AudioBuffer<float>& audio, double rate)
{
    const int frames = audio.getNumSamples();
    const float* l = audio.getReadPointer(0);
    const float* r = audio.getReadPointer(1);
    const double up = 1.0 - std::exp(-1.0 / (0.010 * rate)), down = 1.0 - std::exp(-1.0 / (0.300 * rate));
    std::vector<float> m((size_t) ((frames + kMeterHop - 1) / kMeterHop), 0.0f);
    double env = 0.0;
    for (int pass = 0; pass < 2; ++pass)
        for (int i = 0; i < frames; ++i)
        {
            const double x = 0.5 * (l[i] + r[i]), sq = x * x;
            env += (sq > env ? up : down) * (sq - env);
            if (pass == 1 && i % kMeterHop == 0) m[(size_t) (i / kMeterHop)] = (float) std::sqrt(env);
        }
    const float peak = *std::max_element(m.begin(), m.end());
    if (peak > 0.0f)
        for (auto& v : m) v /= peak;
    return m;
}

float LoopLibrary::level(int loop, double phase) const
{
    if (loop < 0 || loop >= size()) return 0.0f;
    const auto& m = entries[(size_t) loop].meter;
    if (m.empty()) return 0.0f;
    const double x = juce::jlimit(0.0, 1.0, phase) * (double) m.size();
    const int i = juce::jmin((int) m.size() - 1, (int) x);
    const float f = (float) (x - i);
    return m[(size_t) i] + f * (m[(size_t) (i + 1) % m.size()] - m[(size_t) i]);
}

LoopLibrary::LoopLibrary()
{
    int size = 0;
    const char* json = resourceNamed("colours.json", size);
    std::map<juce::String, int> colours;
    const auto colourList = juce::JSON::parse(juce::String::fromUTF8(json, size));
    if (const auto* list = colourList.getArray())
        for (const auto& c : *list) colours[c["file"].toString()] = (int) c["colour"];
    json = resourceNamed("layout.json", size);
    const auto layout = juce::JSON::parse(juce::String::fromUTF8(json, size));
    if (const auto* points = layout.getArray())
    {
        juce::WavAudioFormat wav;
        for (const auto& p : *points)
        {
            Entry e;
            e.name = p["file"].toString();
            e.pos  = { (float) (double) p["x"], (float) (double) p["y"] };
            e.colour = colours.count(e.name) ? colours[e.name] : 0;
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
            // Level every break to the same loudness, as it sounds looping.
            // In float, so the loud-peaked ones keep their transients above 0 dBFS.
            e.audio.applyGain(jm::gainFor(jm::loopLoudness(e.audio.getReadPointer(0), e.audio.getReadPointer(1),
                                                           frames, e.rate)));
            e.meter = meterOf(e.audio, e.rate);
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
