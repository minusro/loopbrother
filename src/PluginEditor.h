#pragma once
#include "PluginProcessor.h"

namespace jmui {

// Yellow and white: a yellow pad in a white frame, white for everything that
// moves or sounds, a deeper yellow for structure (cell borders, ring tracks).
const juce::Colour white  { 0xffffffff };
const juce::Colour yellow { 0xffffc800 };
const juce::Colour shade  { 0xffe0a800 };

// Hides the mouse pointer while a control is being dragged: the control's own
// marker (the puck, a knob's pointer) shows where it is. Every draggable
// control gets one: control.addMouseListener(&hider, false).
class HideCursorWhileDragging : public juce::MouseListener {
public:
    void mouseDown(const juce::MouseEvent& e) override
    {
        saved = e.eventComponent->getMouseCursor();
        e.eventComponent->setMouseCursor(juce::MouseCursor::NoCursor);
    }
    void mouseUp(const juce::MouseEvent& e) override { e.eventComponent->setMouseCursor(saved); }

private:
    juce::MouseCursor saved;
};

// The XY pad: every break sits at its similarity-layout position and owns the
// patch of pad nearest to it. The selection circle mixes every break whose
// patch it covers, by area covered.
class XYPad : public juce::Component {
public:
    explicit XYPad(JungleProcessor& p);
    ~XYPad() override;
    void refresh();                         // poll params + playback state (30 Hz)
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

private:
    static constexpr int kMaxMix = JungleProcessor::kMaxMix;

    juce::Point<float> toScreen(juce::Point<float> u) const;
    juce::Point<float> toUnit(juce::Point<float> s) const;
    void setFromMouse(juce::Point<float> s);

    JungleProcessor&    proc;
    const LoopLibrary&  lib;
    juce::RangedAudioParameter* px = nullptr;
    juce::RangedAudioParameter* py = nullptr;
    HideCursorWhileDragging hideCursor;

    juce::Point<float> puck { -1.0f, -1.0f };
    jm::MixVoice mix[kMaxMix] {};           // what the circle covers
    int mixCount = 0;
    struct Voice { int loop; float gain; double phase; };
    Voice voices[kMaxMix] {};               // what is sounding
    int voiceCount = 0;

    std::vector<juce::Path> cellPaths;      // in component pixels
};

// Square and resizable by the host window (the size is kept in the plugin
// state); everything on the pad scales with it.
//
// The window keeps a 1:1 ratio through JUCE's constrainer. Where JUCE
// controls the window (the standalone) that gives the expected feel:
// dragging a corner keeps the opposite corner in place, dragging an edge
// keeps the opposite edge and grows the two others half each way. In plugin
// hosts the host owns the window and only asks the plugin for a size; JUCE's
// VST3 answer is patched (cmake/juce-vst3-resize.patch) to follow the side
// being dragged. Any non-square size that still arrives (a host that ignores
// the constraint) is squared afterwards along the side that moved.
class JungleEditor : public juce::AudioProcessorEditor, private juce::Timer, private juce::AsyncUpdater {
public:
    static constexpr int kDesignSize  = 480;  // the size drawing dimensions are given at
    static constexpr int kDefaultSize = 960, kMinSize = 240, kMaxSize = 1440;

    explicit JungleEditor(JungleProcessor&);
    ~JungleEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override { pad.refresh(); }
    void handleAsyncUpdate() override;      // square up after an unconstrained host resize

    JungleProcessor& proc;
    XYPad pad;
    int side = kDefaultSize;                // the last square size

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JungleEditor)
};

} // namespace jmui
