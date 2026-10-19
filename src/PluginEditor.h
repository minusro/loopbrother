#pragma once
#include "PluginProcessor.h"

namespace jmui {

// Yellow, white and green: a yellow pad, white for what the circle covers,
// green for the breaks' dots, a deeper yellow for structure (cell borders).
const juce::Colour white  { 0xffffffff };
const juce::Colour yellow { 0xffffc800 };
const juce::Colour shade  { 0xffe0a800 };
const juce::Colour green  { 0xff2e7d32 };

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

// The window's constrainer: square, an edge drag follows that edge, a corner
// drag follows the corner's further-out side, and a corner drag stays a
// corner drag until the drag ends. A host only says which sides changed
// size, and a corner dragged off the diagonal often moves the mouse along
// one axis only for an event. Read alone, that looks like an edge drag of
// the other side, and the window flicked to that side's size and back. Hosts
// tell the plugin nothing about the mouse button, so a drag ends when the
// sizes stop coming for a moment.
class SquareConstrainer : public juce::ComponentBoundsConstrainer {
public:
    static constexpr double kDragGapMs = 400.0;

    SquareConstrainer() { setFixedAspectRatio(1.0); }
    void checkBounds(juce::Rectangle<int>& bounds, const juce::Rectangle<int>& previous,
                     const juce::Rectangle<int>& limits,
                     bool top, bool left, bool bottom, bool right) override;

private:
    double lastCheck = -1.0e9;              // ms
    bool corner = false;                    // this drag is a corner drag...
    bool cTop = false, cLeft = false, cBottom = false, cRight = false; // ...of this corner
};

// Square and resizable by the host window (the size is kept in the plugin
// state); everything on the pad scales with it.
//
// Every route to a new window size asks the SquareConstrainer. In the
// standalone, JUCE draws the window frame and also places the window: a
// corner drag keeps the opposite corner in place, an edge drag keeps the
// opposite edge and grows the two others half each way (with
// cmake/juce-standalone-resize.patch). In plugin hosts the host owns the
// window and only asks for a size, through JUCE's VST3 wrapper
// (cmake/juce-vst3-resize.patch); where it lands is the host's call. Any
// non-square size that still arrives (a host that ignores the constraint)
// is squared afterwards by the same rules.
class JungleEditor : public juce::AudioProcessorEditor, private juce::Timer, private juce::AsyncUpdater {
public:
    static constexpr int kDesignSize  = 480;  // the size drawing dimensions are given at
    static constexpr int kDefaultSize = JungleProcessor::kDefaultEditorSize, kMinSize = 240, kMaxSize = 1440;

    explicit JungleEditor(JungleProcessor&);
    ~JungleEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override { pad.refresh(); }
    void handleAsyncUpdate() override;      // square up after an unconstrained host resize

    JungleProcessor& proc;
    SquareConstrainer square;
    XYPad pad;
    int side = kDefaultSize;                // the last square size

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JungleEditor)
};

} // namespace jmui
