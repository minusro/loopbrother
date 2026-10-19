#include "PluginProcessor.h"
#include "PluginEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;

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
    return { p.begin(), p.end() };
}

JungleProcessor::JungleProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMS", layout())
{
    pX = apvts.getRawParameterValue("x");
    pY = apvts.getRawParameterValue("y");
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
    freeBeat_ = 0.0;
}

void JungleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    buffer.clear();
    if (n == 0 || buffer.getNumChannels() < 2) { midi.clear(); return; }

    // Transport: follow the host; free-run at the loops' own tempo when there
    // is no transport at all (standalone).
    double bpm = jm::kLoopBpm, beat = freeBeat_;
    bool hasTransport = false, playing = true;
    if (auto* phd = getPlayHead())
        if (auto pos = phd->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = *b;
            if (auto q = pos->getPpqPosition())
            {
                hasTransport = true;
                playing      = pos->getIsPlaying();
                beat         = *q;
            }
        }

    // The mix: every break the selection circle overlaps, by area covered.
    // Set before the transport so a start begins straight on it.
    const float x = pX->load(), y = pY->load();
    if (! juce::exactlyEqual(x, lastX_) || ! juce::exactlyEqual(y, lastY_))
    {
        lastX_ = x;
        lastY_ = y;
        mixCount_ = library_->pad().mix({ x, y }, jm::kPuckRadius, mix_, kMaxMix);
    }
    engine_.setMix(mix_, mixCount_);
    engine_.beginBlock(playing, beat, bpm);

    // Note-ons restart the loop at 1.1.1, sample-accurately.
    float* L = buffer.getWritePointer(0);
    float* R = buffer.getWritePointer(1);
    int cur = 0;
    for (const auto meta : midi)
    {
        if (! meta.getMessage().isNoteOn()) continue;
        const int at = juce::jlimit(cur, n, meta.samplePosition);
        engine_.render(L + cur, R + cur, at - cur);
        engine_.restart();
        cur = at;
    }
    engine_.render(L + cur, R + cur, n - cur);
    midi.clear();

    if (! hasTransport) freeBeat_ += n * bpm / 60.0 / sr_;
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
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void JungleProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new JungleProcessor(); }
