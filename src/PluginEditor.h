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
// Plugin hosts get no fixed aspect ratio: JUCE's VST3 size check squares a
// proposed size by its shorter side, so dragging one window edge outwards
// could only ever shrink the window. Instead the editor accepts whatever size
// the host proposes and then squares itself, following the side the host
// moved, so edges grow the window as well as shrink it. The standalone keeps
// JUCE's fixed-ratio constrainer, which handles edge drags properly there.
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
    void handleAsyncUpdate() override;      // square up after a host resize

    JungleProcessor& proc;
    XYPad pad;
    int side = kDefaultSize;                // the last square size

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JungleEditor)
};

} // namespace jmui
