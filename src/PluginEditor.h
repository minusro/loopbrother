#pragma once
#include "PluginProcessor.h"
#include "Cereal.h"

namespace jmui {

// A warm off-white pad and status bar (cream) with white cell borders, the
// breaks in the Froot Loops pastels (Cereal.h) and the logo in the colour
// of the one under the circle, sunk in with a white lower edge. Dark brown
// is for any other lettering.
const juce::Colour white  { 0xffffffff };
const juce::Colour cream  { 0xfffffaf6 };
const juce::Colour brown  { 0xff3e2723 };

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
// - Option (Alt): 16 times slower, and the changes apply at once instead of
//   on the next 16th. An Option press leaves the circle where it is.
// - Shift: sizes the circle like a knob: right or up grows it, left or down
//   shrinks it, the circle staying where it is.
//
// Each cell under the circle shows its break as an Ö: a cereal ring in the
// break's colour with two dots over it (Cereal.h). The dots go round the
// ring to face the way the circle has lately been dragged, and back on top
// after a tap. While the break plays,
// the Ö follows its meter about the ring's centre, from half size in
// silence to double at the loop's peak. Every other cell shows a crumb: a
// round piece the size of one of the Ö's dots at rest.
//
// Dragging pushes the cells the circle's centre passes through (their
// borders and crumbs) a few pixels the way it goes, as through liquid: once,
// then back to rest, no swinging to and fro; cells it didn't cross stay
// still. Drawing only; the sound follows the true cells.
class XYPad : public juce::Component, private juce::Timer {
public:
    static constexpr float kFineSpeed = 1.0f / 16.0f;
    static constexpr float kRingRadius = 8.5f;  // at 480 px
    static constexpr float kCrumbRadius = 0.24f * kRingRadius; // an umlaut dot

    explicit XYPad(JungleProcessor& p);
    ~XYPad() override;
    void refresh();                         // poll params + playback state (30 Hz)
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    bool dragging() const { return isDragging; }
    void setRingStyle(const RingStyle& s) { style = &s; repaint(); } // for trying looks out

private:
    static constexpr int kMaxMix = JungleProcessor::kMaxMix;

    juce::Point<float> toScreen(juce::Point<float> u) const;
    juce::Point<float> toUnit(juce::Point<float> s) const;
    void setPuck(juce::Point<float> u);
    void setCircleSize(float s);

    // The ripples: one push per step of a drag, the way and as far as the
    // circle moved, for the cells its centre passed through.
    struct Drop { double born; juce::Point<float> push; std::vector<int> cells; };
    void addDrop(juce::Point<float> from, juce::Point<float> to);
    void timerCallback() override;          // animates ripples and turning umlauts
    void turnUmlauts(float angle);

public:
    // The break whose cell a point of the pad is in, and how far the ripples
    // move that cell at a time (pad units).
    int cellAt(juce::Point<float> u) const;
    juce::Point<float> circleCentre() const { return puck; }
    juce::Point<float> cellShift(int cell, double now) const;
    // Where the umlauts sit round the ring (radians, clockwise, 0 = on top).
    float umlautTurn() const { return umlautAngle; }

private:

    JungleProcessor&    proc;
    const LoopLibrary&  lib;
    juce::RangedAudioParameter* px = nullptr;
    juce::RangedAudioParameter* py = nullptr;
    juce::RangedAudioParameter* psize = nullptr;

    juce::Point<float> puck { -1.0f, -1.0f };
    float size = -1.0f;                     // Circle Size, 1 = jm::kPuckRadius
    bool  isDragging = false;
    CerealSprites sprites;
    const RingStyle* style = &kRingStyle;
    std::vector<Drop> drops;
    double lastDrop = 0.0;
    juce::Point<float> lastDropAt;
    double lastTick = 0.0;
    float umlautAngle = 0.0f, umlautTarget = 0.0f;
    juce::Point<float> heading;             // the circle's recent moves, pixels
    double headingAt = 0.0;
    float travelled = 0.0f;                 // by the mouse since the press, pixels
    juce::Point<float> last;                // the drag, in pad pixels (unbounded)
    jm::MixVoice mix[kMaxMix] {};           // what the circle covers
    int mixCount = 0;
    struct Voice { int loop; float gain; double phase; };
    Voice voices[kMaxMix] {};               // what is sounding
    int voiceCount = 0;

    std::vector<juce::Path> cellPaths;      // in component pixels
    std::vector<std::vector<std::vector<int>>> corners; // per cell, per corner: the cells meeting there
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

// The square pad over a small status bar with the logo, resizable by the
// host window at that fixed ratio (the width is kept in the plugin state);
// everything scales with it. The logo, in Cherry Bomb One, takes the colour
// of the break whose cell the circle's centre is in, fading as it changes. Holding Command (Control on Windows) with the
// pointer over the plugin mutes it. Command is global on macOS, so it only
// counts over the plugin (or during a pad drag, when the hidden pointer can
// be anywhere): Command-clicks in the host don't mute it.
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
    static constexpr int kTitleHeight = 44;   // the status bar, at kDesignSize
    static constexpr float kLogoKerning = 0.0f;
    static constexpr float kLogoHeight = 38.0f; // its lettering (Cherry Bomb One), at kDesignSize
    static juce::String logo() { return juce::CharPointer_UTF8("L\xc3\x96\xc3\x96P BR\xc3\x96THER"); }
    static constexpr int kDefaultSize = JungleProcessor::kDefaultEditorSize, kMinSize = 240, kMaxSize = 1440;
    static constexpr double kRatio = (double) kDesignSize / (kDesignSize + kTitleHeight); // width / height
    static int heightFor(int width) { return juce::roundToInt(width / kRatio); }

    explicit JungleEditor(JungleProcessor&);
    ~JungleEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void modifierKeysChanged(const juce::ModifierKeys&) override { updateMute(); }
    // For trying fonts out: the logo in this font and spacing (its height is
    // set to fit).
    void setLogoFont(const juce::Font& f) { logoFont = f; repaint(); }

private:
    void timerCallback() override;
    juce::Rectangle<int> statusBar() const { return getLocalBounds().withTop(pad.getBottom()); }
    void handleAsyncUpdate() override;      // fix the ratio after an unconstrained host resize
    void updateMute();

    JungleProcessor& proc;
    WindowConstrainer window { kRatio };
    XYPad pad;
    int side = kDefaultSize;                // the last width at the right ratio
    std::optional<juce::Font> logoFont;
    juce::Typeface::Ptr logoFace;           // Cherry Bomb One, compiled in
    juce::Colour logoColour;                // the colour it's fading to the target's

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JungleEditor)
};

} // namespace jmui
