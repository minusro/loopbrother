#include "PluginEditor.h"

namespace jmui {

// ---------------------------------------------------------------- Ö
// A break's mark: an Ö of radius r at c. The O is a thick ring (a Froot
// Loop), the dots sit over it as on the letter.
struct UmlautO {
    juce::Path ring, dots;
    float hole;
    UmlautO(juce::Point<float> c, float r) : hole(0.48f * r)
    {
        ring.addEllipse(c.x - r, c.y - r, 2 * r, 2 * r);
        ring.addEllipse(c.x - hole, c.y - hole, 2 * hole, 2 * hole);
        ring.setUsingNonZeroWinding(false); // the hole stays empty
        const float d = 0.24f * r, x = 0.5f * r, y = c.y - r - 1.55f * d;
        dots.addEllipse(c.x - x - d, y - d, 2 * d, 2 * d);
        dots.addEllipse(c.x + x - d, y - d, 2 * d, 2 * d);
    }
    juce::Path all() const { juce::Path p(ring); p.addPath(dots); return p; }
};

static void drawShadow(juce::Graphics& g, juce::Point<float> c, float r)
{
    juce::DropShadow(juce::Colours::black.withAlpha(0.16f), juce::jmax(1, juce::roundToInt(0.35f * r)),
                     { juce::roundToInt(0.08f * r), juce::roundToInt(0.16f * r) })
        .drawForPath(g, UmlautO(c, r).all());
}

// The colour a cereal's shadows take: darker, richer and turned a little
// towards red, so yellow shades to orange instead of olive.
juce::Colour shadowOf(juce::Colour base)
{
    return base.withRotatedHue(-0.05f).withMultipliedSaturation(1.3f).withMultipliedBrightness(0.6f);
}

// Shaded like the bevelled letters of the old Google logo: lit from the top
// left, the ring rounded like a tube with a gloss on its upper left, the
// dots like little spheres, a faint grain over it all so it reads as cereal.
static void drawFrootLoop(juce::Graphics& g, juce::Point<float> c, float r, juce::Colour base, const juce::Image& grain)
{
    namespace C = juce::Colours;
    const UmlautO o(c, r);
    const auto all = o.all();
    const auto dark = shadowOf(base);
    g.setColour(base);
    g.fillPath(all);

    juce::ColourGradient tube(dark.withAlpha(0.0f), c, dark.withAlpha(0.35f), { c.x + r, c.y }, true);
    tube.addColour(o.hole / r, dark.withAlpha(0.25f));
    tube.addColour((o.hole / r + 1.0f) / 2.0f, C::white.withAlpha(0.10f));
    g.setGradientFill(tube);
    g.fillPath(o.ring);

    for (const float side : { -1.0f, 1.0f })
    {
        const float d = 0.24f * r;
        const juce::Point<float> dot { c.x + side * 0.5f * r, c.y - r - 1.55f * d };
        juce::ColourGradient sphere(C::white.withAlpha(0.6f), dot.translated(-0.35f * d, -0.35f * d),
                                    dark.withAlpha(0.4f), dot.translated(d, d), true);
        sphere.addColour(0.45, C::white.withAlpha(0.0f));
        g.setGradientFill(sphere);
        g.fillEllipse(dot.x - d, dot.y - d, 2 * d, 2 * d);
    }

    g.setGradientFill(juce::ColourGradient(C::white.withAlpha(0.35f), c.x - r, c.y - r,
                                           dark.withAlpha(0.22f), c.x + r, c.y + r, false));
    g.fillPath(all);

    juce::Path gloss;
    gloss.addCentredArc(c.x, c.y, (o.hole + r) / 2, (o.hole + r) / 2, 0.0f, -1.45f, -0.45f, true);
    g.setColour(C::white.withAlpha(0.7f));
    g.strokePath(gloss, juce::PathStrokeType(0.14f * r, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::FillType texture(grain, juce::AffineTransform::scale(r / 14.0f).translated(c));
    texture.setOpacity(0.5f);
    g.setFillType(texture);
    g.fillPath(all);

    g.setColour(dark.withAlpha(0.5f));
    g.strokePath(all, juce::PathStrokeType(juce::jmax(0.6f, 0.05f * r)));
}

// ---------------------------------------------------------------- pad
XYPad::XYPad(JungleProcessor& p) : proc(p), lib(p.library())
{
    px = p.apvts.getParameter("x");
    py = p.apvts.getParameter("y");
    psize = p.apvts.getParameter("size");

    // The cereal grain, one per colour: speckles of its shadow and of white.
    for (size_t k = 0; k < std::size(grains); ++k)
    {
        const auto dark = shadowOf(frootLoops[k]);
        auto& img = grains[k];
        img = juce::Image(juce::Image::ARGB, 64, 64, true);
        juce::Random rnd(1);
        for (int y = 0; y < img.getHeight(); ++y)
            for (int x = 0; x < img.getWidth(); ++x)
            {
                const float v = rnd.nextFloat();
                img.setPixelAt(x, y, v < 0.5f ? dark.withAlpha(0.5f * (0.5f - v)) : juce::Colours::white.withAlpha(0.4f * (v - 0.5f)));
            }
    }
}

XYPad::~XYPad()
{
    proc.setPadHeld(false);
    proc.setFineDrag(false);
}

juce::Point<float> XYPad::toScreen(juce::Point<float> u) const
{
    return { u.x * (float) getWidth(), (1.0f - u.y) * (float) getHeight() };
}

juce::Point<float> XYPad::toUnit(juce::Point<float> s) const
{
    return { juce::jlimit(0.0f, 1.0f, s.x / (float) getWidth()),
             juce::jlimit(0.0f, 1.0f, 1.0f - s.y / (float) getHeight()) };
}

void XYPad::resized()
{
    cellPaths.clear();
    for (const auto& poly : lib.cells())
    {
        juce::Path path;
        for (size_t i = 0; i < poly.size(); ++i)
        {
            const auto s = toScreen({ (float) poly[i].x, (float) poly[i].y });
            if (i == 0) path.startNewSubPath(s);
            else        path.lineTo(s);
        }
        path.closeSubPath();
        cellPaths.push_back(std::move(path));
    }
}

void XYPad::refresh()
{
    bool changed = false;
    const juce::Point<float> u { px->convertFrom0to1(px->getValue()), py->convertFrom0to1(py->getValue()) };
    const float s = psize->convertFrom0to1(psize->getValue());
    if (u != puck || ! juce::exactlyEqual(s, size))
    {
        puck = u;
        size = s;
        mixCount = lib.pad().mix({ u.x, u.y }, jm::circleRadius(s), mix, kMaxMix);
        changed = true;
    }
    const int n = juce::jmin(proc.uiCount(), kMaxMix);
    changed |= n != voiceCount;
    for (int i = 0; i < n; ++i)
    {
        const Voice v { proc.uiLoop(i), proc.uiGain(i), proc.uiPhase(i) };
        changed |= v.loop != voices[i].loop || std::abs(v.gain - voices[i].gain) > 1e-3f
                || std::abs(v.phase - voices[i].phase) > 1e-4;
        voices[i] = v;
    }
    voiceCount = n;
    if (changed) repaint();
}

void XYPad::paint(juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    g.fillAll(white);

    // cells under the circle, washed in their break's colour by their share
    for (int m = 0; m < mixCount; ++m)
        if (mix[m].loop >= 0 && mix[m].loop < (int) cellPaths.size())
        {
            g.setColour(frootLoop(lib.entry(mix[m].loop).colour).withAlpha(0.10f + 0.35f * mix[m].weight));
            g.fillPath(cellPaths[(size_t) mix[m].loop]);
        }
    const float scale = b.getWidth() / (float) JungleEditor::kDesignSize; // sizes below are at 480 px
    g.setColour(shade.withAlpha(0.45f)); // on white the full shade would be a cage
    for (const auto& path : cellPaths)
        g.strokePath(path, juce::PathStrokeType(juce::jmax(1.0f, scale)));

    // An Ö on each cell under the circle only, its size following the
    // break's meter while it plays: half in silence, double at the peak.
    // Shadows first, so none falls across a neighbouring Ö.
    struct Mark { juce::Point<float> c; float r; int colour; };
    Mark marks[kMaxMix];
    int n = 0;
    for (int m = 0; m < mixCount; ++m)
    {
        const int loop = mix[m].loop;
        if (loop < 0 || loop >= lib.size()) continue;
        float r = kRingRadius * scale;
        for (int i = 0; i < voiceCount; ++i)
            if (voices[i].loop == loop) r = kRingRadius * scale * (0.5f + 1.5f * lib.level(loop, voices[i].phase));
        marks[n++] = { toScreen(lib.entry(loop).pos), r, juce::jlimit(0, 6, lib.entry(loop).colour) };
    }
    for (int i = 0; i < n; ++i) drawShadow(g, marks[i].c, marks[i].r);
    for (int i = 0; i < n; ++i)
        drawFrootLoop(g, marks[i].c, marks[i].r, frootLoop(marks[i].colour), grains[(size_t) marks[i].colour]);
}

void XYPad::setPuck(juce::Point<float> u)
{
    px->setValueNotifyingHost(px->convertTo0to1(u.x));
    py->setValueNotifyingHost(py->convertTo0to1(u.y));
    refresh();
}

void XYPad::setCircleSize(float s)
{
    psize->setValueNotifyingHost(psize->convertTo0to1(juce::jlimit(jm::kCircleSizeMin, jm::kCircleSizeMax, s)));
    refresh();
}

void XYPad::mouseDown(const juce::MouseEvent& e)
{
    for (auto* p : { px, py, psize }) p->beginChangeGesture();
    isDragging = true;
    last = pivot = e.position;
    e.source.enableUnboundedMouseMovement(true);
    proc.setFineDrag(e.mods.isAltDown());
    if (! e.mods.isAltDown()) setPuck(toUnit(e.position));
    proc.setPadHeld(true); // after the move, so an audition starts on the pressed spot
}

void XYPad::mouseDrag(const juce::MouseEvent& e)
{
    const bool fine = e.mods.isAltDown();
    proc.setFineDrag(fine); // before the change, so it applies at once
    const float speed = fine ? kFineSpeed : 1.0f;
    if (e.mods.isShiftDown())
    {
        // Moving away from the pivot grows the circle by as much (in pixels).
        const float grow = (e.position.getDistanceFrom(pivot) - last.getDistanceFrom(pivot)) * speed;
        setCircleSize(size + grow / (float) (jm::kPuckRadius * getWidth()));
    }
    else
    {
        const auto before = toScreen(puck);
        setPuck(toUnit(before + (e.position - last) * speed));
        pivot += toScreen(puck) - before; // with the circle, stopping where it stops
    }
    last = e.position;
}

void XYPad::mouseUp(const juce::MouseEvent& e)
{
    proc.setPadHeld(false);
    proc.setFineDrag(false);
    isDragging = false;
    for (auto* p : { px, py, psize }) p->endChangeGesture();
    // Show the pointer again on the circle's centre. The unbounded drag ends
    // first: ending it afterwards, JUCE would put the pointer back near
    // where it left the pad.
    auto source = e.source; // a handle: the copy moves the same pointer
    source.enableUnboundedMouseMovement(false);
    source.setScreenPosition(localPointToGlobal(toScreen(puck)));
}

// ---------------------------------------------------------------- window
void WindowConstrainer::checkBounds(juce::Rectangle<int>& bounds, const juce::Rectangle<int>& previous,
                                    const juce::Rectangle<int>& limits,
                                    bool top, bool left, bool bottom, bool right)
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (now - lastCheck > kDragGapMs) corner = false; // a new drag
    lastCheck = now;
    if ((top || bottom) && (left || right))
    {
        corner = true;
        cTop = top, cLeft = left, cBottom = bottom, cRight = right;
    }
    else if (corner && (top || bottom || left || right))
        top = cTop, left = cLeft, bottom = cBottom, right = cRight;
    // JUCE's fixed-ratio rules then do the rest: an edge sets the size, a
    // corner takes the larger of the two sides (so the pointer stays on the
    // window's edge) and both place the window.
    ComponentBoundsConstrainer::checkBounds(bounds, previous, limits, top, left, bottom, right);
}

// ---------------------------------------------------------------- editor
JungleEditor::JungleEditor(JungleProcessor& p) : AudioProcessorEditor(&p), proc(p), pad(p)
{
    // Read the saved size before anything resizes the editor: resized()
    // records every width it sees.
    const int width = juce::jlimit(kMinSize, kMaxSize, p.editorSize());
    addAndMakeVisible(pad);
    window.setSizeLimits(kMinSize, heightFor(kMinSize), kMaxSize, heightFor(kMaxSize));
    setConstrainer(&window);
    setResizable(true, false);
    setSize(width, heightFor(width));
    pad.refresh();
    startTimerHz(30);
}

JungleEditor::~JungleEditor()
{
    stopTimer();
    proc.setMuteHeld(false);
}

void JungleEditor::paint(juce::Graphics& g)
{
    g.fillAll(white);
    const auto bar = getLocalBounds().withTop(pad.getBottom()).toFloat();
    g.setColour(shade); // the pad's lower edge
    g.fillRect(bar.withHeight(juce::jmax(1.0f, (float) getWidth() / kDesignSize)));
    g.setColour(forest);
    g.setFont(juce::Font(juce::FontOptions(bar.getHeight() * 0.62f, juce::Font::bold).withKerningFactor(0.14f)));
    g.drawText(juce::String(juce::CharPointer_UTF8("L\xc3\x96\xc3\x96P BR\xc3\x96THER")), bar,
               juce::Justification::centred, false);
}

void JungleEditor::resized()
{
    const int w = getWidth(), h = getHeight();
    const int padSide = juce::jmin(w, h);
    pad.setBounds(0, 0, padSide, padSide); // the status bar takes the rest
    if (std::abs(h - heightFor(w)) > 1)
    {
        // A host that ignored the constraint: fix the ratio once it has
        // finished applying its size (a resize from inside it would not
        // reach the window).
        triggerAsyncUpdate();
        return;
    }
    side = w;
    proc.setEditorSize(w);
}

void JungleEditor::handleAsyncUpdate()
{
    auto r = getLocalBounds();
    if (std::abs(r.getHeight() - heightFor(r.getWidth())) <= 1) return;
    // The sides that differ from the last good size are the ones dragged.
    const juce::Rectangle<int> was { side, heightFor(side) };
    window.checkBounds(r, was, {}, false, false, r.getHeight() != was.getHeight(), r.getWidth() != side);
    setSize(r.getWidth(), r.getHeight());
}

void JungleEditor::updateMute()
{
    const bool command = juce::ModifierKeys::getCurrentModifiersRealtime().isCommandDown();
    proc.setMuteHeld(command && (isMouseOver(true) || pad.dragging()));
}

} // namespace jmui
