// End-to-end check of the built plugin: hosts the VST3 with a scripted
// transport and MIDI, and compares the output against the break file read
// straight from disk.
//   cmake -B build -DJM_BUILD_HOST_CHECK=ON && cmake --build build --target jm_host_check
//   build/jm_host_check_artefacts/Release/jm_host_check "<path to Jungle Massive.vst3>" breaks
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>

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

int main(int argc, char** argv)
{
    if (argc < 3) { std::printf("usage: jm_host_check <plugin.vst3> <breaks dir>\n"); return 2; }
    juce::ScopedJuceInitialiser_GUI gui;

    // The break at the pad position we will set (the first in the layout).
    const juce::File breaks(juce::String::fromUTF8(argv[2]));
    const auto layout = juce::JSON::parse(breaks.getChildFile("layout.json"));
    const auto first  = (*layout.getArray())[0];
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> rd(fm.createReaderFor(breaks.getChildFile(first["file"].toString())));
    juce::AudioBuffer<float> ref(2, (int) rd->lengthInSamples);
    rd->read(&ref, 0, ref.getNumSamples(), 0, true, true);
    const int    frames = ref.getNumSamples();
    const double loopBeats = 4.0 * juce::roundToInt(frames / (kSr * 240.0 / kBpm));

    // The file's value at a position in beats from the loop's 1.1.1 (the loop
    // spans exactly loopBeats, so positions between samples are interpolated).
    auto expectAt = [&](double beats)
    {
        double ph = beats / loopBeats;
        ph -= std::floor(ph);
        const double x = ph * frames;
        const int    i = std::min(frames - 1, (int) x);
        const float  f = (float) (x - i);
        auto at = [&](int k) { return ref.getSample(0, ((k % frames) + frames) % frames); };
        const float y0 = at(i - 1), y1 = at(i), y2 = at(i + 1), y3 = at(i + 2);
        return (((0.5f * (y3 - y0) + 1.5f * (y1 - y2)) * f + (y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3)) * f
                + 0.5f * (y2 - y0)) * f + y1;
    };

    // Load the plugin and put the puck on that break.
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
    for (auto* prm : plugin->getParameters())
    {
        if (prm->getName(32) == "Pad X") prm->setValue((float) (double) first["x"]);
        if (prm->getName(32) == "Pad Y") prm->setValue((float) (double) first["y"]);
    }

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
    // Does out[from..] match the file from `beats` into the loop on?
    auto matches = [&](size_t from, double beats)
    {
        float worst = 0.0f;
        for (size_t i = from; i < out.size(); ++i)
            worst = std::max(worst, std::abs(out[i] - expectAt(beats + (double) (i - from) * kBeatPerSample)));
        return worst < 1e-4f;
    };
    auto silentFrom = [&](size_t from)
    {
        for (size_t i = from; i < out.size(); ++i) if (out[i] != 0.0f) return false;
        return true;
    };

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

    plugin->releaseResources();
    std::printf(failures == 0 ? "host check passed (%s)\n" : "host check FAILED (%s)\n",
                first["file"].toString().toRawUTF8());
    return failures == 0 ? 0 : 1;
}
