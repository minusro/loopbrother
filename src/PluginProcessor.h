#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "LoopLibrary.h"
#include "engine/LoopEngine.h"
#include "engine/Transport.h"

class JungleProcessor : public juce::AudioProcessor {
public:
    JungleProcessor();

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                 { return true; }
    const juce::String getName() const override     { return juce::CharPointer_UTF8(JucePlugin_Name); }
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

    // The pad is held: with the transport stopped, that plays the mix from
    // 1.1.1 until it is let go (as does holding a MIDI note).
    void setPadHeld(bool held) { padHeld_.store(held, std::memory_order_relaxed); }

    // A fine drag (Option held): pad changes apply at once instead of
    // waiting for the next 16th.
    void setFineDrag(bool fine) { fineDrag_.store(fine, std::memory_order_relaxed); }

    // Command held over the plugin mutes the instrument at once (and unmutes
    // it as soon as it is let go). Auditions included.
    void setMuteHeld(bool held) { muteHeld_.store(held, std::memory_order_relaxed); }
    bool muteHeld() const       { return muteHeld_.load(std::memory_order_relaxed); }

    // The Circle Size parameter range: skewed so size 1 sits in the middle.
    static juce::NormalisableRange<float> circleSizeRange();

    // The editor's width (it is square), kept in the plugin state. A new
    // instance, or a session saved before there was a size, opens at 480.
    static constexpr int kDefaultEditorSize = 480;
    int  editorSize() const          { return editorSize_.load(); }
    void setEditorSize(int size)     { editorSize_.store(size); }

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
    jm::TransportFollower timeline_;
    double sr_ = 48000.0;
    std::atomic<bool> padHeld_ { false }, fineDrag_ { false }, muteHeld_ { false };
    std::atomic<int>  editorSize_ { kDefaultEditorSize };
    bool noteDown_[16][128] {};   // held MIDI notes, per channel
    int  heldNotes_ = 0;

    // the mix under the puck, recomputed when it moves
    jm::MixVoice mix_[kMaxMix] {};
    int   mixCount_ = 0;
    float lastX_ = -1.0f, lastY_ = -1.0f, lastSize_ = -1.0f;

    std::atomic<int>    uiCount_ { 0 };
    std::atomic<int>    uiLoop_[kMaxMix] {};
    std::atomic<float>  uiGain_[kMaxMix] {};
    std::atomic<double> uiPhase_[kMaxMix] {};

    std::atomic<float>* pX = nullptr;
    std::atomic<float>* pY = nullptr;
    std::atomic<float>* pSize = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JungleProcessor)
};
