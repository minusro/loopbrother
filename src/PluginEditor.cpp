#include "PluginEditor.h"

namespace jmui {

// ---------------------------------------------------------------- pad
XYPad::XYPad(JungleProcessor& p) : proc(p), lib(p.library())
{
    px = p.apvts.getParameter("x");
    py = p.apvts.getParameter("y");
    psize = p.apvts.getParameter("size");
}

XYPad::~XYPad()
{
    stopTimer();
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
    const float scale = b.getWidth() / (float) JungleEditor::kDesignSize; // sizes below are at 480 px
    g.fillAll(cream);
    sprites.configure(kRingRadius * scale, kCrumbRadius * scale,
                      g.getInternalContext().getPhysicalPixelScaleFactor(), *style);

    // Where everything is drawn: the layout, moved by any passing ripple.
    const double now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    const bool rippling = ! drops.empty();
    auto place = [&](juce::Point<float> u) { return toScreen(rippling ? u + ripple(u, now) : u); };
    std::vector<juce::Path> rippled;
    if (rippling)
        for (const auto& poly : lib.cells())
        {
            juce::Path path;
            for (size_t i = 0; i < poly.size(); ++i)
            {
                const auto s = place({ (float) poly[i].x, (float) poly[i].y });
                if (i == 0) path.startNewSubPath(s);
                else        path.lineTo(s);
            }
            path.closeSubPath();
            rippled.push_back(std::move(path));
        }
    const auto& cells = rippling ? rippled : cellPaths;

    // cells under the circle, washed in their break's colour by their share
    std::vector<char> covered((size_t) lib.size(), 0);
    for (int m = 0; m < mixCount; ++m)
        if (mix[m].loop >= 0 && mix[m].loop < (int) cells.size())
        {
            covered[(size_t) mix[m].loop] = 1;
            g.setColour(frootLoop(lib.entry(mix[m].loop).colour).withAlpha(0.10f + 0.35f * mix[m].weight));
            g.fillPath(cells[(size_t) mix[m].loop]);
        }
    g.setColour(white);
    for (const auto& path : cells)
        g.strokePath(path, juce::PathStrokeType(juce::jmax(2.0f, 2.0f * scale)));

    // A crumb on every cell the circle doesn't cover...
    for (int i = 0; i < lib.size(); ++i)
        if (! covered[(size_t) i])
            sprites.drawCrumb(g, place(lib.entry(i).pos), lib.entry(i).colour);

    // ...and an Ö on each one it does, its size following the break's meter
    // while it plays: half in silence, double at the peak.
    struct Mark { juce::Point<float> c; float r; int loop; };
    std::vector<Mark> marks;
    for (int m = 0; m < mixCount; ++m)
    {
        const int loop = mix[m].loop;
        if (loop < 0 || loop >= lib.size()) continue;
        float r = kRingRadius * scale;
        for (int i = 0; i < voiceCount; ++i)
            if (voices[i].loop == loop) r = kRingRadius * scale * (0.5f + 1.5f * lib.level(loop, voices[i].phase));
        marks.push_back({ toScreen(lib.entry(loop).pos), r, loop }); // the circle's own marks hold still
    }
    for (const auto& k : marks) sprites.drawRing(g, k.c, k.r, lib.entry(k.loop).colour, k.loop);
}

// ---------------------------------------------------------------- ripples
// Each step of a drag gives the cells around the circle one push the way
// the circle went: a single smooth swell out and back to rest (sin^2, never
// below zero, so nothing swings past and back), reaching a cell later the
// further it is (kRippleSpeed) and only near the circle (kRippleReach). A
// steady drag keeps the neighbours leaning forward; stop and they settle.
// Pad units and seconds: kRipplePush is how far a cell moves per unit the
// circle moved, kRippleMax the most any cell moves.
constexpr float kRippleSpeed = 0.5f, kRippleReach = 0.12f, kRippleSwell = 0.22f,
                kRipplePush = 0.14f, kRippleMax = 0.014f;

void XYPad::addDrop(juce::Point<float> push)
{
    const double now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    drops.push_back({ puck, now, push });
    if (drops.size() > 64) drops.erase(drops.begin());
    lastDrop = now;
    lastDropAt = puck;
    if (! isTimerRunning()) startTimerHz(60);
}

juce::Point<float> XYPad::ripple(juce::Point<float> u, double now) const
{
    juce::Point<float> d;
    for (const auto& drop : drops)
    {
        const float dist = (u - drop.at).getDistanceFromOrigin();
        if (dist > 3.0f * kRippleReach) continue;
        const float t = (float) (now - drop.born) - dist / kRippleSpeed; // since the swell reached here
        if (t <= 0.0f || t >= kRippleSwell) continue;
        const float swell = std::sin(juce::MathConstants<float>::pi * t / kRippleSwell);
        d += drop.push * (kRipplePush * swell * swell * std::exp(-(dist / kRippleReach) * (dist / kRippleReach)));
    }
    const float m = d.getDistanceFromOrigin(); // a fast flick moves cells further, but only so far
    return m > 0.0f ? d * (kRippleMax * std::tanh(m / kRippleMax) / m) : d;
}

void XYPad::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    drops.erase(std::remove_if(drops.begin(), drops.end(),
                               [&](const Drop& d) { return now - d.born > kRippleSwell + 3.0f * kRippleReach / kRippleSpeed; }),
                drops.end());
    if (drops.empty()) stopTimer();
    repaint();
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
    lastDropAt = puck;
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

    // A push where the circle is now and then, as far as it went since.
    const double now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    const auto moved = puck - lastDropAt;
    if (now - lastDrop > 0.06 && moved.getDistanceFromOrigin() * (float) getWidth() > 1.0f) addDrop(moved);
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
    g.setColour(cream); // the pad's lower edge
    g.fillRect(bar.withHeight(juce::jmax(1.0f, (float) getWidth() / kDesignSize)));
    g.setColour(brown);
    g.setFont(juce::Font(juce::FontOptions(kLogoHeight * (float) getWidth() / kDesignSize, juce::Font::bold)
                             .withKerningFactor(0.14f)));
    g.drawText(logo(), bar,
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
