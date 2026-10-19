// End-to-end check of the built plugin: hosts the VST3 with a scripted
// transport and MIDI, and compares the output against the break files read
// straight from disk.
//   cmake -B build -DJM_BUILD_HOST_CHECK=ON && cmake --build build --target jm_host_check
//   build/jm_host_check_artefacts/Release/jm_host_check "<path to Jungle Massive.vst3>" breaks
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "../src/engine/PadGeometry.h"
#include <map>

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

constexpr double kSr = 44100.0, kBpm = 162.0, kBeatPerSample = kBpm / 60.0 / kSr;
constexpr int    kBlock = 512, kFade = 221; // 5 ms

struct ScriptedPlayHead : juce::AudioPlayHead {
    double ppq = 0.0;
    bool playing = false;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setBpm(kBpm);
        p.setPpqPosition(ppq);
        p.setIsPlaying(playing);
        p.setTimeSignature(TimeSignature { 4, 4 });
        return p;
    }
};

// A break read from disk. at() is its value at a position in beats from the
// loop's 1.1.1 (the loop spans exactly its bars, so positions between samples
// are interpolated).
struct Ref {
    juce::AudioBuffer<float> audio;
    double beats = 4.0;
    float at(double pos) const
    {
        const int frames = audio.getNumSamples();
        double ph = pos / beats;
        ph -= std::floor(ph);
        const double x = ph * frames;
        const int    i = std::min(frames - 1, (int) x);
        const float  f = (float) (x - i);
        auto s = [&](int k) { return audio.getSample(0, ((k % frames) + frames) % frames); };
        const float y0 = s(i - 1), y1 = s(i), y2 = s(i + 1), y3 = s(i + 2);
        return (((0.5f * (y3 - y0) + 1.5f * (y1 - y2)) * f + (y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3)) * f
                + 0.5f * (y2 - y0)) * f + y1;
    }
};

int main(int argc, char** argv)
{
    if (argc < 3) { std::printf("usage: jm_host_check <plugin.vst3> <breaks dir>\n"); return 2; }
    juce::ScopedJuceInitialiser_GUI gui;

    // The pad, rebuilt from the layout with the plugin's own geometry code.
    const juce::File breaks(juce::String::fromUTF8(argv[2]));
    const auto layout = juce::JSON::parse(breaks.getChildFile("layout.json"));
    std::vector<jm::Pt> sites;
    for (const auto& p : *layout.getArray()) sites.push_back({ (double) p["x"], (double) p["y"] });
    jm::PadMap pad;
    pad.setSites(sites);

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::map<int, Ref> refs;
    auto ref = [&](int i) -> const Ref&
    {
        auto it = refs.find(i);
        if (it != refs.end()) return it->second;
        Ref r;
        std::unique_ptr<juce::AudioFormatReader> rd(
            fm.createReaderFor(breaks.getChildFile((*layout.getArray())[i]["file"].toString())));
        r.audio.setSize(2, (int) rd->lengthInSamples);
        rd->read(&r.audio, 0, r.audio.getNumSamples(), 0, true, true);
        r.beats = 4.0 * juce::roundToInt(r.audio.getNumSamples() / (kSr * 240.0 / kBpm));
        return refs.emplace(i, std::move(r)).first->second;
    };

    // Load the plugin.
    juce::AudioPluginFormatManager pfm;
    pfm.addFormat(new juce::VST3PluginFormat());
    juce::OwnedArray<juce::PluginDescription> descs;
    juce::VST3PluginFormat().findAllTypesForFile(descs, juce::String::fromUTF8(argv[1]));
    if (descs.isEmpty()) { std::printf("could not load %s\n", argv[1]); return 2; }
    juce::String err;
    auto plugin = pfm.createPluginInstance(*descs[0], kSr, kBlock, err);
    if (! plugin) { std::printf("instantiate failed: %s\n", err.toRawUTF8()); return 2; }

    ScriptedPlayHead head;
    plugin->setPlayHead(&head);
    plugin->setPlayConfigDetails(0, 2, kSr, kBlock);
    plugin->prepareToPlay(kSr, kBlock);

    // Put the puck at p; returns the mix the circle should produce there.
    std::vector<jm::MixVoice> mix;
    auto puckAt = [&](jm::Pt p)
    {
        for (auto* prm : plugin->getParameters())
        {
            if (prm->getName(32) == "Pad X") prm->setValue((float) p.x);
            if (prm->getName(32) == "Pad Y") prm->setValue((float) p.y);
        }
        jm::MixVoice m[jm::LoopEngine::kMaxMix];
        const int n = pad.mix({ (double) (float) p.x, (double) (float) p.y }, jm::kPuckRadius, m, jm::LoopEngine::kMaxMix);
        mix.assign(m, m + n);
    };

    // Run blocks, collecting channel 0; noteAt >= 0 sends a note-on at that
    // sample of the first block.
    std::vector<float> out;
    auto run = [&](int blocks, int noteAt = -1)
    {
        out.clear();
        juce::AudioBuffer<float> buf(2, kBlock);
        for (int b = 0; b < blocks; ++b)
        {
            juce::MidiBuffer midi;
            if (b == 0 && noteAt >= 0) midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100), noteAt);
            buf.clear();
            plugin->processBlock(buf, midi);
            out.insert(out.end(), buf.getReadPointer(0), buf.getReadPointer(0) + kBlock);
            if (head.playing) head.ppq += kBlock * kBeatPerSample;
        }
    };
    // Does out[from..] match the mix, starting `beats` into the loops?
    auto matches = [&](size_t from, double beats)
    {
        float worst = 0.0f;
        for (size_t i = from; i < out.size(); ++i)
        {
            const double pos = beats + (double) (i - from) * kBeatPerSample;
            float want = 0.0f;
            for (const auto& v : mix) want += v.weight * ref(v.loop).at(pos);
            worst = std::max(worst, std::abs(out[i] - want));
        }
        return worst < 1e-4f;
    };
    auto silentFrom = [&](size_t from)
    {
        for (size_t i = from; i < out.size(); ++i) if (out[i] != 0.0f) return false;
        return true;
    };

    // 0. The puck on the first break: the circle sits inside its cell.
    puckAt(sites[0]);
    CHECK(mix.size() == 1 && mix[0].loop == 0, "puck on a break selects that break alone");

    // 1. Transport stopped: silence.
    run(20);
    CHECK(silentFrom(0), "stopped transport: no output");

    // 2. Start at bar 1: the loop from its first sample, wrapping past its end.
    head.playing = true; head.ppq = 0.0;
    run(400); // ~4.6 s, longer than the 2-bar loop
    CHECK(matches(0, 0.0), "playback from bar 1 equals the break, wrapped");

    // 3. Note-on at sample 300 of a block: the loop restarts at 1.1.1 there.
    run(40, 300);
    CHECK(matches(300 + kFade, kFade * kBeatPerSample), "note-on restarts the loop at its first sample");

    // 4. Stop: silent after the fade.
    head.playing = false;
    run(10);
    CHECK(silentFrom(kFade), "transport stop: silent after the fade");

    // 5. Start again at beat 9: synced to the song position, not the old restart.
    head.playing = true; head.ppq = 9.0;
    run(100);
    CHECK(matches(kFade, 9.0 + kFade * kBeatPerSample), "start at beat 9 chases the song position");

    // 6. The puck on and around the border with the nearest neighbour: both
    //    breaks play, weighted by the circle's area in each cell. Off-centre
    //    positions catch any quantisation of the pad parameters.
    {
        size_t nb = 1;
        for (size_t i = 1; i < sites.size(); ++i)
            if (std::hypot(sites[i].x - sites[0].x, sites[i].y - sites[0].y)
                < std::hypot(sites[nb].x - sites[0].x, sites[nb].y - sites[0].y)) nb = i;
        const jm::Pt mid { (sites[0].x + sites[nb].x) / 2, (sites[0].y + sites[nb].y) / 2 };
        bool split = true, ok = true;
        for (const double dx : { 0.0, -0.003, 0.0017 })
            for (const double dy : { 0.0, 0.0023 })
            {
                puckAt({ mid.x + dx, mid.y + dy });
                split &= mix.size() >= 2;
                const double beat = head.ppq;
                run(40);
                ok &= matches(kFade, beat + kFade * kBeatPerSample);
            }
        CHECK(split, "puck near a border overlaps two cells");
        CHECK(ok, "border mixes play both breaks by area share");
    }

    plugin->releaseResources();
    std::printf(failures == 0 ? "host check passed\n" : "host check FAILED\n");
    return failures == 0 ? 0 : 1;
}
