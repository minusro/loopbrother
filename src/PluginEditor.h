#pragma once
#include "PluginProcessor.h"

namespace jmui {

// Yellow, white and green: a yellow pad, white for what the circle covers
// and for the breaks' dots while nothing plays, green for the dots while
// the loops play, a deeper yellow for structure (cell borders), and a
// forest green logo on the white title bar.
const juce::Colour white  { 0xffffffff };
const juce::Colour yellow { 0xffffc800 };
const juce::Colour shade  { 0xffe0a800 };
const juce::Colour green  { 0xff17be19 };  // midway between forest and pure green
const juce::Colour forest { 0xff2e7d32 };

// Hides the mouse pointer while a control is being dragged: the control's own
// marker (a knob's pointer) shows where it is. Every draggable control gets
// one: control.addMouseListener(&hider, false). The pad hides it through
// JUCE's unbounded mouse movement instead, which a fine drag needs.
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
//
// A press puts the circle's centre there; dragging then moves it by as much
// as the mouse moves. The pointer is hidden and the drag can run past the
// screen edge. On release the pointer reappears on the circle's centre.
// - Command (Control on Windows): 10 times slower, and the changes apply at
//   once instead of on the next 16th. A Command press leaves the circle
//   where it is.
// - Shift: the circle grows as the mouse moves away from the pivot and
//   shrinks as it moves towards it. The pivot starts at the press point and
//   moves with the circle while Shift is up.
class XYPad : public juce::Component {
public:
    static constexpr float kFineSpeed = 0.1f;

    explicit XYPad(JungleProcessor& p);
    ~XYPad() override;
    void refresh();                         // poll params + playback state (30 Hz)
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    bool dragging() const { return isDragging; }

private:
    static constexpr int kMaxMix = JungleProcessor::kMaxMix;

    juce::Point<float> toScreen(juce::Point<float> u) const;
    juce::Point<float> toUnit(juce::Point<float> s) const;
    void setPuck(juce::Point<float> u);
    void setCircleSize(float s);

    JungleProcessor&    proc;
    const LoopLibrary&  lib;
    juce::RangedAudioParameter* px = nullptr;
    juce::RangedAudioParameter* py = nullptr;
    juce::RangedAudioParameter* psize = nullptr;

    juce::Point<float> puck { -1.0f, -1.0f };
    float size = -1.0f;                     // Circle Size, 1 = jm::kPuckRadius
    bool  playing = false;
    bool  isDragging = false;
    juce::Point<float> last, pivot;         // the drag, in pad pixels (unbounded)
    jm::MixVoice mix[kMaxMix] {};           // what the circle covers
    int mixCount = 0;
    struct Voice { int loop; float gain; double phase; };
    Voice voices[kMaxMix] {};               // what is sounding
    int voiceCount = 0;

    std::vector<juce::Path> cellPaths;      // in component pixels
};

// The window's constrainer: a fixed ratio, an edge drag follows that edge, a corner
// drag follows the corner's further-out side, and a corner drag stays a
// corner drag until the drag ends. A host only says which sides changed
// size, and a corner dragged off the diagonal often moves the mouse along
// one axis only for an event. Read alone, that looks like an edge drag of
// the other side, and the window flicked to that side's size and back. Hosts
// tell the plugin nothing about the mouse button, so a drag ends when the
// sizes stop coming for a moment.
class WindowConstrainer : public juce::ComponentBoundsConstrainer {
public:
    static constexpr double kDragGapMs = 400.0;

    explicit WindowConstrainer(double widthOverHeight) { setFixedAspectRatio(widthOverHeight); }
    void checkBounds(juce::Rectangle<int>& bounds, const juce::Rectangle<int>& previous,
                     const juce::Rectangle<int>& limits,
                     bool top, bool left, bool bottom, bool right) override;

private:
    double lastCheck = -1.0e9;              // ms
    bool corner = false;                    // this drag is a corner drag...
    bool cTop = false, cLeft = false, cBottom = false, cRight = false; // ...of this corner
};

// A small white title bar with the logo over the square pad, resizable by
// the host window at that fixed ratio (the width is kept in the plugin
// state); everything scales with it. Holding Option (Alt) with the pointer
// over the plugin mutes it while the clock runs. Option is global on macOS,
// so it only counts over the plugin (or during a pad drag, when the hidden
// pointer can be anywhere): Option-clicks in the host don't mute it.
//
// Every route to a new window size asks the WindowConstrainer. In the
// standalone, JUCE draws the window frame and also places the window: a
// corner drag keeps the opposite corner in place, an edge drag keeps the
// opposite edge and grows the two others half each way (with
// cmake/juce-standalone-resize.patch). In plugin hosts the host owns the
// window and only asks for a size, through JUCE's VST3 wrapper
// (cmake/juce-vst3-resize.patch); where it lands is the host's call. Any
// off-ratio size that still arrives (a host that ignores the constraint) is
// corrected afterwards by the same rules.
class JungleEditor : public juce::AudioProcessorEditor, private juce::Timer, private juce::AsyncUpdater {
public:
    static constexpr int kDesignSize  = 480;  // the pad width drawing dimensions are given at
    static constexpr int kTitleHeight = 24;   // at kDesignSize
    static constexpr int kDefaultSize = JungleProcessor::kDefaultEditorSize, kMinSize = 240, kMaxSize = 1440;
    static constexpr double kRatio = (double) kDesignSize / (kDesignSize + kTitleHeight); // width / height
    static int heightFor(int width) { return juce::roundToInt(width / kRatio); }

    explicit JungleEditor(JungleProcessor&);
    ~JungleEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void modifierKeysChanged(const juce::ModifierKeys&) override { updateMute(); }

private:
    void timerCallback() override { pad.refresh(); updateMute(); }
    void handleAsyncUpdate() override;      // fix the ratio after an unconstrained host resize
    void updateMute();

    JungleProcessor& proc;
    WindowConstrainer window { kRatio };
    XYPad pad;
    int side = kDefaultSize;                // the last width at the right ratio

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JungleEditor)
};

} // namespace jmui
