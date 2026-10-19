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

    // For the editor, published every block: the loops being heard, their
    // level and how far through each the playhead is (0..1).
    static constexpr int kMaxMix = jm::LoopEngine::kMaxMix;
    int    uiCount() const        { return uiCount_.load(std::memory_order_relaxed); }
    int    uiLoop(int i) const    { return uiLoop_[i].load(std::memory_order_relaxed); }
    float  uiGain(int i) const    { return uiGain_[i].load(std::memory_order_relaxed); }
    double uiPhase(int i) const   { return uiPhase_[i].load(std::memory_order_relaxed); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();

    juce::SharedResourcePointer<LoopLibrary> library_;
    jm::LoopEngine engine_;
    double sr_ = 48000.0, freeBeat_ = 0.0;

    // the mix under the puck, recomputed when it moves
    jm::MixVoice mix_[kMaxMix] {};
    int   mixCount_ = 0;
    float lastX_ = -1.0f, lastY_ = -1.0f;

    std::atomic<int>    uiCount_ { 0 };
    std::atomic<int>    uiLoop_[kMaxMix] {};
    std::atomic<float>  uiGain_[kMaxMix] {};
    std::atomic<double> uiPhase_[kMaxMix] {};

    std::atomic<float>* pX = nullptr;
    std::atomic<float>* pY = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JungleProcessor)
};
