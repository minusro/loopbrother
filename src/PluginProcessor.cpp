#include "PluginProcessor.h"
#include "PluginEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;

juce::NormalisableRange<float> JungleProcessor::circleSizeRange()
{
    juce::NormalisableRange<float> r { jm::kCircleSizeMin, jm::kCircleSizeMax };
    r.setSkewForCentre(1.0f);
    return r;
}

APVTS::ParameterLayout JungleProcessor::layout()
{
    // Continuous: the short AudioParameterFloat constructor would snap to
    // steps of 0.01, too coarse for the pad and its area mix.
    const juce::NormalisableRange<float> unit { 0.0f, 1.0f };
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "x", 1 }, "Pad X", unit, 0.5f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "y", 1 }, "Pad Y", unit, 0.5f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "size", 1 }, "Circle Size", circleSizeRange(), 1.0f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction([](float v, int) { return juce::String(v, 2) + "x"; })
            .withValueFromStringFunction([](const juce::String& s) { return s.retainCharacters("0123456789.").getFloatValue(); })));
    return { p.begin(), p.end() };
}

JungleProcessor::JungleProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMS", layout())
{
    pX = apvts.getRawParameterValue("x");
    pY = apvts.getRawParameterValue("y");
    pSize = apvts.getRawParameterValue("size");
    engine_.setLoops(library_->loops(), library_->size());
}

bool JungleProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void JungleProcessor::prepareToPlay(double sampleRate, int)
{
    sr_ = sampleRate;
    engine_.prepare(sampleRate);
    outEnv_ = 0.0;
    outUp_ = 1.0 - std::exp(-1.0 / (0.001 * sampleRate));
    outDown_ = 1.0 - std::exp(-1.0 / (0.300 * sampleRate));
    timeline_ = {};
    for (auto& ch : noteDown_) std::fill(std::begin(ch), std::end(ch), false);
    heldNotes_ = 0;
}

void JungleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    buffer.clear();
    if (n == 0 || buffer.getNumChannels() < 2) { midi.clear(); return; }

    // Timeline: the host transport while it plays; with it stopped, an
    // audition from 1.1.1 while the pad or a MIDI note is held; a free run at
    // the loops' own tempo when there is no transport at all (standalone).
    double bpm = jm::kLoopBpm, hostBeat = 0.0;
    bool hasTransport = false, hostPlaying = false;
    if (auto* phd = getPlayHead())
        if (auto pos = phd->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = *b;
            if (auto q = pos->getPpqPosition())
            {
                hasTransport = true;
                hostPlaying  = pos->getIsPlaying();
                hostBeat     = *q;
            }
        }
    if (bpm <= 0.0) bpm = jm::kLoopBpm;
    const bool padHeld = padHeld_.load();

    // The mix: every break the selection circle overlaps, its share the area
    // covered, blended at equal power. Set before the timeline so a start
    // begins straight on it. A fine drag's changes apply at once.
    const float x = pX->load(), y = pY->load(), size = pSize->load();
    bool moved = false;
    if (! juce::exactlyEqual(x, lastX_) || ! juce::exactlyEqual(y, lastY_) || ! juce::exactlyEqual(size, lastSize_))
    {
        lastX_ = x;
        lastY_ = y;
        lastSize_ = size;
        mixCount_ = library_->pad().mix({ x, y }, jm::circleRadius(size), mix_, kMaxMix);
        jm::equalPower(mix_, mixCount_);
        moved = true;
    }
    engine_.setMix(mix_, mixCount_, moved && fineDrag_.load(std::memory_order_relaxed));

    // Command over the plugin mutes, clock running or not (auditions too).
    engine_.setMuted(muteHeld_.load(std::memory_order_relaxed));

    // Render in segments between note events, so a note-on restarts the loops
    // at 1.1.1 on its exact sample, and with the transport stopped the first
    // held note starts an audition there and the last one released ends it.
    float* L = buffer.getWritePointer(0);
    float* R = buffer.getWritePointer(1);
    const double beatsPerSample = bpm / 60.0 / sr_;
    int cur = 0;
    bool playing = false;
    double endBeat = 0.0; // the timeline's beat at the block's end
    auto renderTo = [&](int end)
    {
        const auto tl = timeline_.next(hasTransport, hostPlaying, hostBeat + cur * beatsPerSample, bpm,
                                       padHeld || heldNotes_ > 0, end - cur, sr_);
        engine_.beginBlock(tl.playing, tl.beat, bpm, tl.resync);
        engine_.render(L + cur, R + cur, end - cur);
        playing = tl.playing;
        endBeat = tl.beat + (end - cur) * beatsPerSample;
        cur = end;
    };
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        const int at = juce::jlimit(cur, n, meta.samplePosition);
        if (msg.isNoteOn())
        {
            renderTo(at);
            bool& down = noteDown_[(msg.getChannel() - 1) & 15][msg.getNoteNumber() & 127];
            if (! down) { down = true; ++heldNotes_; }
            engine_.restart(); // no-op when nothing plays yet: the audition starts from 1.1.1 anyway
        }
        else if (msg.isNoteOff() || msg.isAllNotesOff() || msg.isAllSoundOff())
        {
            renderTo(at);
            if (msg.isNoteOff())
            {
                bool& down = noteDown_[(msg.getChannel() - 1) & 15][msg.getNoteNumber() & 127];
                if (down) { down = false; --heldNotes_; }
            }
            else
            {
                for (auto& ch : noteDown_) std::fill(std::begin(ch), std::end(ch), false);
                heldNotes_ = 0;
            }
        }
    }
    renderTo(n);
    midi.clear();

    double loudest = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double s = 0.5 * (L[i] + R[i]), sq = s * s;
        outEnv_ += (sq > outEnv_ ? outUp_ : outDown_) * (sq - outEnv_);
        loudest = std::max(loudest, outEnv_);
    }
    const float level = (float) std::sqrt(loudest);
    if (level > uiOutput_.load(std::memory_order_relaxed)) uiOutput_.store(level, std::memory_order_relaxed);
    uiPlaying_.store(playing, std::memory_order_relaxed);
    if (playing)
    {
        uiBpm_.store(bpm, std::memory_order_relaxed);
        uiBeatOrigin_.store(endBeat - juce::Time::getMillisecondCounterHiRes() * bpm / 60000.0, std::memory_order_relaxed);
    }

    int loops[kMaxMix];
    float gains[kMaxMix];
    double phases[kMaxMix];
    const int count = engine_.sounding(loops, gains, phases, kMaxMix);
    for (int i = 0; i < count; ++i)
    {
        uiLoop_[i].store(loops[i], std::memory_order_relaxed);
        uiGain_[i].store(gains[i], std::memory_order_relaxed);
        uiPhase_[i].store(phases[i], std::memory_order_relaxed);
    }
    uiCount_.store(count, std::memory_order_relaxed);
}

juce::AudioProcessorEditor* JungleProcessor::createEditor() { return new jmui::JungleEditor(*this); }

void JungleProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty("editorSize", editorSize_.load(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void JungleProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
        {
            auto state = juce::ValueTree::fromXml(*xml);
            if (state.hasProperty("editorSize")) editorSize_.store((int) state.getProperty("editorSize"));
            state.removeProperty("editorSize", nullptr);
            apvts.replaceState(state);
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new JungleProcessor(); }
