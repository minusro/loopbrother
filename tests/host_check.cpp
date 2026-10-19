// End-to-end check of the built plugin: hosts the VST3 with a scripted
// transport and MIDI, and compares the output against the break files read
// straight from disk (levelled to the same loudness).
//   cmake -B build -DJM_BUILD_HOST_CHECK=ON && cmake --build build --target jm_host_check
//   build/jm_host_check_artefacts/Release/jm_host_check "<path to Jungle Massive.vst3>" breaks
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "../src/engine/Loudness.h"
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
        r.audio.applyGain(jm::gainFor(jm::loopLoudness(r.audio.getReadPointer(0), r.audio.getReadPointer(1),
                                                       r.audio.getNumSamples(), kSr)));
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

    // Put the puck at p with the circle at a size; returns the mix the circle
    // should produce there. (The Circle Size range is the plugin's: skewed
    // so 1 sits in the middle.)
    std::vector<jm::MixVoice> mix;
    const auto sizeRange = []
    {
        juce::NormalisableRange<float> r { jm::kCircleSizeMin, jm::kCircleSizeMax };
        r.setSkewForCentre(1.0f);
        return r;
    }();
    auto puckAt = [&](jm::Pt p, float size = 1.0f)
    {
        const float norm = sizeRange.convertTo0to1(size);
        for (auto* prm : plugin->getParameters())
        {
            if (prm->getName(32) == "Pad X") prm->setValue((float) p.x);
            if (prm->getName(32) == "Pad Y") prm->setValue((float) p.y);
            if (prm->getName(32) == "Circle Size") prm->setValue(norm);
        }
        jm::MixVoice m[jm::LoopEngine::kMaxMix];
        const int n = pad.mix({ (double) (float) p.x, (double) (float) p.y },
                              jm::circleRadius(sizeRange.convertFrom0to1(norm)), m, jm::LoopEngine::kMaxMix);
        jm::equalPower(m, n);
        mix.assign(m, m + n);
    };

    // Run blocks, collecting channel 0; noteAt >= 0 sends a note-on at that
    // sample of the first block, offBlock >= 0 its note-off at offAt.
    std::vector<float> out;
    auto run = [&](int blocks, int noteAt = -1, int offBlock = -1, int offAt = 0)
    {
        out.clear();
        juce::AudioBuffer<float> buf(2, kBlock);
        for (int b = 0; b < blocks; ++b)
        {
            juce::MidiBuffer midi;
            if (b == 0 && noteAt >= 0) midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100), noteAt);
            if (b == offBlock) midi.addEvent(juce::MidiMessage::noteOff(1, 60), offAt);
            buf.clear();
            plugin->processBlock(buf, midi);
            out.insert(out.end(), buf.getReadPointer(0), buf.getReadPointer(0) + kBlock);
            if (head.playing) head.ppq += kBlock * kBeatPerSample;
        }
    };
    // Does out[from..] match the mix, starting `beats` into the loops?
    // (`to` limits the range, `with` checks against another mix than the current.)
    auto matches = [&](size_t from, double beats, size_t to = SIZE_MAX, const std::vector<jm::MixVoice>* with = nullptr)
    {
        float worst = 0.0f;
        for (size_t i = from; i < std::min(to, out.size()); ++i)
        {
            const double pos = beats + (double) (i - from) * kBeatPerSample;
            float want = 0.0f;
            for (const auto& v : with ? *with : mix) want += v.weight * ref(v.loop).at(pos);
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
    //    (Its note-off follows a block later; a held note would keep playing
    //    through the stop below.)
    run(40, 300, 1, 0);
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
    //    breaks play, at equal-power gains from the circle's area in each
    //    cell. Off-centre positions catch any quantisation of the pad
    //    parameters. Each move waits for the next 16th: the previous mix holds
    //    until the ramp just before it, the new one is complete on it.
    {
        size_t nb = 1;
        for (size_t i = 1; i < sites.size(); ++i)
            if (std::hypot(sites[i].x - sites[0].x, sites[i].y - sites[0].y)
                < std::hypot(sites[nb].x - sites[0].x, sites[nb].y - sites[0].y)) nb = i;
        const jm::Pt mid { (sites[0].x + sites[nb].x) / 2, (sites[0].y + sites[nb].y) / 2 };
        bool split = true, held = true, landed = true;
        // Move (or resize) and check the old mix holds until the 16th and
        // the new one is complete on it.
        auto moveTo = [&](jm::Pt p, float size)
        {
            const auto prev = mix;
            puckAt(p, size);
            const double beat = head.ppq;
            const double step = std::ceil(beat * 4.0 - 1e-6) / 4.0; // transport started at 9: grid from beat 0
            const size_t at = (size_t) std::ceil((step - beat) / kBeatPerSample);
            run(40);
            held   &= at <= (size_t) kFade || matches(0, beat, at - kFade, &prev);
            landed &= matches(at + 1, beat + (double) (at + 1) * kBeatPerSample);
        };
        for (const double dx : { 0.0, -0.003, 0.0017 })
            for (const double dy : { 0.0, 0.0023 })
            {
                moveTo({ mid.x + dx, mid.y + dy }, 1.0f);
                split &= mix.size() >= 2;
            }
        CHECK(split, "puck near a border overlaps two cells");
        CHECK(held, "a pad move waits for the next 16th");
        CHECK(landed, "border mixes land on the 16th, both breaks at equal power by area share");

        // 6b. Circle Size: growing the circle there covers more cells, each
        //     at its share of the bigger circle, on the next 16th like a move.
        held = landed = true;
        const size_t before = mix.size();
        moveTo({ mid.x + 0.0017, mid.y + 0.0023 }, 2.8f);
        std::printf("  Circle Size 2.8: %zu cells (%zu at 1)\n", mix.size(), before);
        CHECK(mix.size() > before, "a bigger circle covers more cells");
        moveTo({ mid.x + 0.0017, mid.y + 0.0023 }, 0.3f);
        CHECK(held && landed, "circle size changes land on the 16th at the new area shares");
        // 6c. The largest circle, in the middle: half the pad, every break
        //     under it playing at its share.
        held = landed = true;
        moveTo({ 0.5, 0.5 }, jm::kCircleSizeMax);
        std::printf("  largest circle: %zu breaks\n", mix.size());
        CHECK(mix.size() > 50, "the largest circle covers about half the breaks");
        CHECK(held && landed, "...and plays every one of them at its share, on the 16th");
        moveTo({ mid.x + 0.0017, mid.y + 0.0023 }, 1.0f);
    }

    // 7. Transport stopped: holding a note auditions the mix from 1.1.1 on the
    //    note-on's sample, at full level; releasing it stops.
    head.playing = false;
    run(10); // let the transport stop fade out
    run(40, 300, 30, 100);
    {
        const size_t on = 300, off = 30 * kBlock + 100;
        bool before = true;
        for (size_t i = 0; i < on; ++i) before &= out[i] == 0.0f;
        CHECK(before, "stopped, no note: silent");
        CHECK(matches(on, 0.0, off), "held note auditions the mix from 1.1.1");
        CHECK(silentFrom(off + kFade), "note-off ends the audition");
    }

    // 8. Window resizing through JUCE's own VST3 host window, which asks the
    //    plugin's checkSizeConstraint the way hosts do: dragging one edge out
    //    or in must follow that edge and stay square, and a corner dragged off
    //    the diagonal must follow its further-out side without flicking to the
    //    nearer one when the mouse moves along one axis only. (The window is
    //    shown fully transparent, so nothing appears on screen.)
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed(plugin->createEditor());
        juce::Component holder;
        holder.setOpaque(false);
        holder.addAndMakeVisible(ed.get());
        holder.setSize(ed->getWidth(), ed->getHeight());
        holder.addToDesktop(juce::ComponentPeer::windowIsTemporary);
        holder.setAlpha(0.0f);
        holder.setVisible(true);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
        // The window is the 480 px pad under a title bar: its ratio, the
        // height for a width (H) and the width for a height (W).
        const int s0 = ed->getWidth(), h0 = ed->getHeight();
        const double ratio = s0 / (double) h0;
        auto H = [&](int w) { return juce::roundToInt(w / ratio); };
        auto W = [&](int h) { return juce::roundToInt(h * ratio); };
        bool ok = s0 == 480 && h0 == 504;
        auto drag = [&](int w, int h, int want)
        {
            ed->setSize(w, h);
            juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
            const bool good = ed->getWidth() == want && std::abs(ed->getHeight() - H(want)) <= 1; // a height-driven size keeps its height
            if (! good) std::printf("  resize %dx%d -> %dx%d, wanted %dx%d\n", w, h, ed->getWidth(), ed->getHeight(), want, H(want));
            ok &= good;
        };
        drag(s0 + 100, h0, s0 + 100);                       // right edge out
        drag(s0 + 100, H(s0 + 100) + 150, W(H(s0 + 100) + 150)); // bottom edge out
        const int s1 = ed->getWidth();
        drag(s1 - 50, H(s1), s1 - 50);                      // right edge in
        drag(s1 - 50, H(s1 - 50) - 120, W(H(s1 - 50) - 120)); // bottom edge in
        CHECK(ok, "VST3 window: 480x504 to start, edges resize along the dragged side at the window's ratio");
        juce::Thread::sleep(500);                           // a new drag, by a corner
        ok = true;
        const int c0 = ed->getWidth(), d0 = ed->getHeight();
        drag(c0 + 20, d0 + 5, c0 + 20);                     // out, x ahead
        drag(c0 + 20, d0 + 10, c0 + 20);                    // the mouse moved down only
        drag(c0 + 30, d0 + 10, c0 + 30);                    // across only
        drag(c0 + 30, d0 + 60, W(d0 + 60));                 // y overtakes
        drag(c0 + 20, d0 + 60, W(d0 + 60));                 // back in across only: y still the further side
        drag(c0 + 20, d0 + 45, W(d0 + 45));
        CHECK(ok, "VST3 window corner drags off the diagonal follow the further side, no flicker");
        holder.removeFromDesktop();
        holder.removeChildComponent(ed.get());
    }

    plugin->releaseResources();
    std::printf(failures == 0 ? "host check passed\n" : "host check FAILED\n");
    return failures == 0 ? 0 : 1;
}
