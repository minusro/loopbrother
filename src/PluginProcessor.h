#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "LoopLibrary.h"
#include "engine/LoopEngine.h"

class JungleProcessor : public juce::AudioProcessor {
public:
    JungleProcessor();

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                 { return true; }
    const juce::String getName() const override     { return "Jungle Massive"; }
    bool acceptsMidi() const override               { return true; }
    bool producesMidi() const override              { return false; }
    bool isMidiEffect() const override              { return false; }
    double getTailLengthSeconds() const override    { return 0.0; }
    int getNumPrograms() override                   { return 1; }
    int getCurrentProgram() override                { return 0; }
    void setCurrentProgram(int) override            {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    const LoopLibrary& library() const { return *library_; }

    // For the editor, published every block: the loop being heard (-1 when
    // silent) and how far through it the playhead is (0..1).
    int    uiSounding() const { return uiSounding_.load(std::memory_order_relaxed); }
    double uiPhase() const    { return uiPhase_.load(std::memory_order_relaxed); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();

    juce::SharedResourcePointer<LoopLibrary> library_;
    jm::LoopEngine engine_;
    double sr_ = 48000.0, freeBeat_ = 0.0;

    std::atomic<int>    uiSounding_ { -1 };
    std::atomic<double> uiPhase_ { 0.0 };

    std::atomic<float>* pX = nullptr;
    std::atomic<float>* pY = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JungleProcessor)
};
