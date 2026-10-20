#pragma once
#include "PluginProcessor.h"
#include "Cereal.h"

namespace jmui {

// A warm off-white pad (cream) inside a cereal bowl: the status bar is its
// front (biscuit, a shade deeper, with a white rim), the breaks in the Froot
// Loops pastels (Cereal.h), and the logo pressed into the bowl: a little
// deeper again (biscuitShade), with a white lower edge. Dark brown is for
// any other lettering.
const juce::Colour white  { 0xffffffff };
const juce::Colour cream  { 0xfffffaf6 };
const juce::Colour biscuit { 0xfffaf4ed }; // the bowl's front, a shade under the pad: the logo's white edge shows on it
const juce::Colour biscuitShade { 0xffe1d9cf }; // the logo: biscuit's hue, 10% darker and a touch richer, so it stays warm
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

// The status bar is the front of a cereal bowl: its top edge is the bowl's
// near lip, an arc of an ellipse lowest at the middle of the window and
// rising kBowlRise (at 480 px) towards its sides, over the pad's lower
// corners. How far above the bar's top the lip is at x, for a window w wide
// and a pad scaled by scale:
constexpr float kBowlRise = 22.0f, kBowlReach = 0.6f; // the ellipse's half-width, in window widths
// With inside > 0 the same for the curve concentric with it that many pixels
// further in (below the lip at the middle): its ellipse has the same centre,
// both semi-axes longer by that much.
inline float bowlLip(float x, float w, float scale, float inside = 0.0f)
{
    const float a = kBowlReach * w, end = 0.5f / kBowlReach;
    const float b = kBowlRise * scale / (1.0f - std::sqrt(1.0f - end * end)); // so it rises kBowlRise at the sides
    const float u = (x - 0.5f * w) / (a + inside);
    return (b + inside) * (1.0f - std::sqrt(juce::jmax(0.0f, 1.0f - u * u)));
}

// The XY pad: every break sits at its similarity-layout position and owns the
// patch of pad nearest to it. The selection circle mixes every break whose
// patch it covers, by area covered.
//
// A press puts the circle's centre there; dragging then moves it by as much
// as the mouse moves. The pointer is hidden and the drag can run past the
// screen edge. On release the pointer reappears on the circle's centre.
// - Option (Alt): 8 times slower, and the changes apply at once instead of
//   on the next 16th. An Option press leaves the circle where it is.
// - Shift, or dragging with the right button: sizes the circle like a knob:
//   right or up grows it, left or down shrinks it, the circle staying where
//   it is. A right press leaves the circle where it is, so a right drag
//   can start anywhere.
//
// Each cell under the circle shows its break as a cereal ring in its
// colour (Cereal.h). While the break plays, the ring follows its meter, from
// half size in silence to double at the loop's peak. Every other cell shows
// a small ring, half the size of one at rest.
//
// Dragging pushes the cells the circle's centre passes through (their
// small rings) a few pixels the way it goes, and the cells
// beside its path outwards, like a wake in liquid; growing or shrinking the
// circle (Shift) pushes the cells round its edge outwards or inwards. A
// pushed cell swings back past where it was and out again a few times,
// dying away, and its small ring squashes along the push and across it in
// step, as if bobbing in milk; cells further off stay still. Drawing only;
// the sound follows the true cells. While the mute is held the circle's
// rings are dark grey and still.
class XYPad : public juce::Component, private juce::Timer {
public:
    static constexpr float kFineSpeed = 1.0f / 8.0f;
    static constexpr float kRingRadius = 8.5f;  // at 480 px
    static constexpr float kSmallO = 0.5f * kRingRadius; // the ring of a cell outside the circle
    static constexpr float kRippleMax = 0.008f; // the most a ripple swings a cell (pad units)...
    static constexpr float kBob = 0.2f;         // ...and its small ring's squash then

    explicit XYPad(JungleProcessor& p);
    ~XYPad() override;
    void refresh();                         // poll params + playback state (30 Hz)
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    bool dragging() const { return isDragging; }
    // Not where the bowl's lip covers it: a press there is on the bowl.
    bool hitTest(int x, int y) override
    {
        return (float) y < (float) getHeight() - bowlLip((float) x, (float) getWidth(), (float) getWidth() / 480.0f);
    }
    void setRingStyle(const RingStyle& s) { style = &s; repaint(); } // for trying looks out

private:
    static constexpr int kMaxMix = JungleProcessor::kMaxMix;

    juce::Point<float> toScreen(juce::Point<float> u) const;
    juce::Point<float> toUnit(juce::Point<float> s) const;
    void setPuck(juce::Point<float> u);
    void setCircleSize(float s);

    // The ripples: one push per step of a drag, the way and as far as the
    // circle moved, for the cells its centre passed through and those beside
    // its path, or as far as its edge moved, for the cells round it.
    struct Drop { double born; std::vector<std::pair<int, juce::Point<float>>> pushes; }; // cell, its push
    void addDrop(juce::Point<float> from, juce::Point<float> to);
    void addSizeDrop(float from, float to); // the circle's radius, pad units
    void ripple(Drop drop, double now);
    // How much of a push the cell can take now without swinging further
    // than the most a cell swings.
    juce::Point<float> limitPush(int cell, juce::Point<float> push, double now) const;
    void timerCallback() override;          // animates the ripples

public:
    // The break whose cell a point of the pad is in, and how far the ripples
    // move that cell at a time (pad units).
    int cellAt(juce::Point<float> u) const;
    juce::Point<float> circleCentre() const { return puck; }
    float circleSize() const { return size; }
    juce::Point<float> cellShift(int cell, double now) const;
    // How squashed that cell's small O is at a time: its width along axis
    // (radians, pad units, y up), 1 round, down to 1 - kBob.
    float cellBob(int cell, double now, float& axis) const;

private:

    JungleProcessor&    proc;
    const LoopLibrary&  lib;
    juce::RangedAudioParameter* px = nullptr;
    juce::RangedAudioParameter* py = nullptr;
    juce::RangedAudioParameter* psize = nullptr;

    juce::Point<float> puck { -1.0f, -1.0f };
    float size = -1.0f;                     // Circle Size, 1 = jm::kPuckRadius
    bool  isDragging = false;
    bool  muted = false;                    // the mute held: the circle's Ös turn grey
    CerealSprites sprites;
    const RingStyle* style = &kRingStyle;
    std::vector<Drop> drops;
    double lastDrop = 0.0;
    juce::Point<float> lastDropAt;
    float lastDropRadius = 0.0f;
    juce::Point<float> last;                // the drag, in pad pixels (unbounded)
    jm::MixVoice mix[kMaxMix] {};           // what the circle covers
    int mixCount = 0;
    struct Voice { int loop; float gain; double phase; };
    Voice voices[kMaxMix] {};               // what is sounding
    int voiceCount = 0;

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
// everything scales with it. As the circle nears its largest, a cat slides
// up from the bottom over the logo, begging for the cereal. The logo, in
// Cherry Bomb One, is pressed into the bowl: a shade deeper than it, with a
// white lower edge. Holding Command (Control on Windows) with the
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
    // The cat in front of the logo: it starts to rise once the circle's
    // radius passes kCatFrom of its largest and is fully up at the largest,
    // easing with kCatEase (seconds). kCatHeight is its height at 480 px,
    // kCatShown how much of it clears the window's bottom edge when it's up
    // (half: its middle on the edge). Each time it comes up it picks a new
    // spot between kCatLeft and kCatRight. While the loops play it sways
    // left and right like a pendulum, kCatSway (radians) each way about
    // where it stands on the edge: at one side on each beat, lingering
    // there for kCatHold of a beat, easing out of it and through the middle
    // to the other side for the next; upright again (over kCatEase) when
    // they stop.
    static constexpr float kCatFrom = 0.6f, kCatHeight = 200.0f, kCatShown = 0.5f;
    static constexpr float kCatLeft = 0.3f, kCatRight = 0.7f; // where its centre may come up, across the width
    static constexpr float kCatSway = 0.07f, kCatHold = 0.15f;
    static constexpr double kCatEase = 0.1;
    float catRise() const { return cat; } // 0 hidden .. 1 up
    float catTurn() const { return catAngle; } // its sway now, radians
    static float catSwayAt(double beat);       // where the sway is at a beat: 1 one way .. -1 the other
    static constexpr float kLogoHeight = 38.0f; // its lettering (Cherry Bomb One), at kDesignSize
    static juce::String logo() { return juce::CharPointer_UTF8("L\xc3\x96\xc3\x96P BR\xc3\x96THER"); }
    static constexpr int kDefaultSize = JungleProcessor::kDefaultEditorSize, kMinSize = 240, kMaxSize = 1440;
    static constexpr double kRatio = (double) kDesignSize / (kDesignSize + kTitleHeight); // width / height
    static int heightFor(int width) { return juce::roundToInt(width / kRatio); }

    explicit JungleEditor(JungleProcessor&);
    ~JungleEditor() override;
    void paint(juce::Graphics&) override;
    void paintOverChildren(juce::Graphics&) override; // the bowl's front and the logo, over the pad
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
    juce::Image catImage;                   // the cat, cut out (images/loops-cat.png)
    float cat = 0.0f;                       // how far it has risen
    float catX = 0.5f;                      // its centre, as a fraction of the width
    float catAngle = 0.0f, catSwaying = 0.0f; // its sway, and how much it sways (0 still .. 1 playing)
    juce::Rectangle<float> catBounds() const; // where it's drawn, before the sway
    juce::AffineTransform catTurned(float angle) const;   // the sway, about where it stands
    double catTick = 0.0;
    float catTarget() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JungleEditor)
};

} // namespace jmui
