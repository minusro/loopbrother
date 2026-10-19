#include "PluginEditor.h"
#include "Assets.h"

namespace jmui {

// ---------------------------------------------------------------- pad
XYPad::XYPad(JungleProcessor& p) : proc(p), lib(p.library())
{
    px = p.apvts.getParameter("x");
    py = p.apvts.getParameter("y");
    psize = p.apvts.getParameter("size");

    // Which cells meet at each corner of each cell: the ones whose breaks
    // are nearest to it (a Voronoi corner is equally far from all of them).
    for (const auto& poly : lib.cells())
    {
        std::vector<std::vector<int>> owners;
        for (const auto& v : poly)
        {
            double nearest = std::numeric_limits<double>::max();
            std::vector<double> dist;
            for (int i = 0; i < lib.size(); ++i)
            {
                dist.push_back(std::hypot(v.x - lib.entry(i).pos.x, v.y - lib.entry(i).pos.y));
                nearest = std::min(nearest, dist.back());
            }
            std::vector<int> meet;
            for (int i = 0; i < lib.size(); ++i)
                if (dist[(size_t) i] < nearest + 1e-5) meet.push_back(i);
            owners.push_back(std::move(meet));
        }
        corners.push_back(std::move(owners));
    }
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
    changed |= n != voiceCount || proc.muteHeld() != muted;
    muted = proc.muteHeld();
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

    // Where everything is drawn: the layout, moved by the ripples. A pushed
    // cell's crumb moves with it; a border corner moves by the average of
    // the cells that meet there, so the borders bend but never come apart.
    const double now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    const bool rippling = ! drops.empty();
    std::vector<juce::Point<float>> shift((size_t) lib.size());
    if (rippling)
        for (int i = 0; i < lib.size(); ++i) shift[(size_t) i] = cellShift(i, now);
    std::vector<juce::Path> rippled;
    if (rippling)
        for (size_t c = 0; c < lib.cells().size(); ++c)
        {
            const auto& poly = lib.cells()[c];
            juce::Path path;
            for (size_t i = 0; i < poly.size(); ++i)
            {
                juce::Point<float> d;
                for (const int m : corners[c][i]) d += shift[(size_t) m];
                d /= (float) juce::jmax<size_t>(1, corners[c][i].size());
                const auto s = toScreen(juce::Point<float> { (float) poly[i].x, (float) poly[i].y } + d);
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
            sprites.drawCrumb(g, toScreen(lib.entry(i).pos + shift[(size_t) i]), lib.entry(i).colour);

    // ...and an Ö on each one it does, its size following the break's meter
    // while it plays: half in silence, double at the peak.
    struct Mark { juce::Point<float> c; float r; int loop; };
    std::vector<Mark> marks;
    for (int m = 0; m < mixCount; ++m)
    {
        const int loop = mix[m].loop;
        if (loop < 0 || loop >= lib.size()) continue;
        float r = kRingRadius * scale;
        if (! muted) // silenced, they rest at their own size
            for (int i = 0; i < voiceCount; ++i)
                if (voices[i].loop == loop) r = kRingRadius * scale * (0.5f + 1.5f * lib.level(loop, voices[i].phase));
        marks.push_back({ toScreen(lib.entry(loop).pos), r, loop }); // the circle's own marks hold still
    }
    for (const auto& k : marks) // dark grey while muted
        sprites.drawRing(g, k.c, k.r, muted ? kMutedColour : lib.entry(k.loop).colour, k.loop, umlautAngle);
}

// ---------------------------------------------------------------- ripples
// Each step of a drag pushes the cells the circle's centre passed through
// since the last step the way the circle went, and the cells beside that
// stretch outwards, away from it, as a wake: a single smooth swell out and
// back to rest (sin^2, never below zero, so nothing swings past and back).
// While the circle crosses a cell it keeps leaning forward; once it has
// passed, the cell settles. Pad units and seconds: kRipplePush is how far a
// crossed cell moves per unit the circle moved, kRippleMax the most any
// cell moves; cells beside the path get kSidePush of that, straight
// outwards, falling off with their distance from the stretch, across it and
// past its ends (kSideWidth), none beyond kSideReach.
constexpr float kRippleSwell = 0.22f, kRipplePush = 0.14f, kRippleMax = 0.014f;
constexpr float kSidePush = 1.5f, kSideWidth = 0.1f, kSideReach = 0.2f;

int XYPad::cellAt(juce::Point<float> u) const
{
    int best = 0;
    float bestDist = std::numeric_limits<float>::max();
    for (int i = 0; i < lib.size(); ++i)
    {
        const float d = u.getDistanceSquaredFrom(lib.entry(i).pos);
        if (d < bestDist) { bestDist = d; best = i; }
    }
    return best;
}

void XYPad::addDrop(juce::Point<float> from, juce::Point<float> to)
{
    const double now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    const auto step = to - from;
    const float length = step.getDistanceFromOrigin();
    Drop drop { now, {} };
    auto pushed = [&drop](int c) { for (const auto& p : drop.pushes) if (p.first == c) return true; return false; };
    // The cells along the way, sampled finely enough to catch a corner.
    const int samples = juce::jmax(1, (int) std::ceil(length / 0.004f));
    for (int i = 0; i <= samples; ++i)
    {
        const int c = cellAt(from + step * ((float) i / (float) samples));
        if (! pushed(c)) drop.pushes.push_back({ c, step });
    }
    // The cells beside it, outwards: the further from the path, the less.
    if (length > 0.0f)
    {
        const auto along = step / length;
        const juce::Point<float> across { -along.y, along.x };
        for (int i = 0; i < lib.size(); ++i)
        {
            if (pushed(i)) continue;
            const auto rel = lib.entry(i).pos - from;
            const float on = rel.getDotProduct(along), off = rel.getDotProduct(across);
            const float past = on < 0.0f ? -on : std::max(0.0f, on - length); // beyond the stretch's ends
            if (std::hypot(off, past) > kSideReach) continue;
            const float fall = std::exp(-(off * off + past * past) / (kSideWidth * kSideWidth));
            drop.pushes.push_back({ i, across * ((off >= 0.0f ? 1.0f : -1.0f) * length * kSidePush * fall) });
        }
    }
    drops.push_back(std::move(drop));
    if (drops.size() > 64) drops.erase(drops.begin());
    lastDrop = now;
    lastDropAt = to;
    if (! isTimerRunning())
    {
        lastTick = now;
        startTimerHz(60);
    }
}

juce::Point<float> XYPad::cellShift(int cell, double now) const
{
    juce::Point<float> d;
    for (const auto& drop : drops)
    {
        const float t = (float) (now - drop.born);
        if (t <= 0.0f || t >= kRippleSwell) continue;
        const float swell = std::sin(juce::MathConstants<float>::pi * t / kRippleSwell);
        for (const auto& [c, push] : drop.pushes)
            if (c == cell) d += push * (kRipplePush * swell * swell);
    }
    const float m = d.getDistanceFromOrigin(); // a fast flick moves cells further, but only so far
    return m > 0.0f ? d * (kRippleMax * std::tanh(m / kRippleMax) / m) : d;
}

// The umlauts turn towards the drag's heading with this time constant, so
// they swing round rather than jump; the heading itself averages the
// circle's moves over about kHeadingTime.
constexpr double kTurnTime = 0.08, kHeadingTime = 0.15;

void XYPad::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    const double dt = juce::jlimit(0.0, 0.1, now - lastTick);
    lastTick = now;
    drops.erase(std::remove_if(drops.begin(), drops.end(), [&](const Drop& d) { return now - d.born > kRippleSwell; }),
                drops.end());
    const float left = std::remainder(umlautTarget - umlautAngle, juce::MathConstants<float>::twoPi); // the short way round
    if (std::abs(left) < 1e-3f) umlautAngle = umlautTarget;
    else umlautAngle += left * (float) (1.0 - std::exp(-dt / kTurnTime));
    if (drops.empty() && juce::exactlyEqual(umlautAngle, umlautTarget)) stopTimer();
    repaint();
}

void XYPad::turnUmlauts(float angle)
{
    umlautTarget = angle;
    if (! isTimerRunning())
    {
        lastTick = juce::Time::getMillisecondCounterHiRes() / 1000.0;
        startTimerHz(60);
    }
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
    travelled = 0.0f;
    heading = {};
    last = e.position;
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
        // Like a knob: right or up grows the circle, left or down shrinks
        // it, its radius by as many pixels as the mouse moved.
        const auto d = e.position - last;
        setCircleSize(size + (d.x - d.y) * speed / (float) (jm::kPuckRadius * getWidth()));
    }
    else
    {
        const auto before = toScreen(puck);
        setPuck(toUnit(before + (e.position - last) * speed));
        const auto step = toScreen(puck) - before;

        // The umlauts face where the circle has been heading lately.
        const double t = juce::Time::getMillisecondCounterHiRes() / 1000.0;
        heading = heading * (float) std::exp(-(t - headingAt) / kHeadingTime) + step;
        headingAt = t;
        if (heading.getDistanceFromOrigin() > 2.0f) turnUmlauts(std::atan2(heading.x, -heading.y));
    }
    travelled += e.position.getDistanceFrom(last);
    last = e.position;

    // A push where the circle is now and then, as far as it went since.
    const double now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    const auto moved = puck - lastDropAt;
    if (now - lastDrop > 0.06 && moved.getDistanceFromOrigin() * (float) getWidth() > 1.0f) addDrop(lastDropAt, puck);
}

void XYPad::mouseUp(const juce::MouseEvent& e)
{
    proc.setPadHeld(false);
    proc.setFineDrag(false);
    isDragging = false;
    for (auto* p : { px, py, psize }) p->endChangeGesture();
    if (travelled < 3.0f) turnUmlauts(0.0f); // a tap: umlauts back on top
    // Show the pointer again on the circle's centre. The unbounded drag ends
    // first: ending it afterwards, JUCE would put the pointer back near
    // where it left the pad.
    auto source = e.source; // a handle: the copy moves the same pointer
    if (source.isUnboundedMouseMovementEnabled())
    {
        source.enableUnboundedMouseMovement(false);
        source.setScreenPosition(localPointToGlobal(toScreen(puck)));
    }
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
    logoFace = juce::Typeface::createSystemTypefaceFor(Assets::CherryBombOneRegular_ttf,
                                                       (size_t) Assets::CherryBombOneRegular_ttfSize);
    pad.refresh();
    logoColour = logoFrom = logoTo = frootLoop(p.library().entry(pad.cellAt(pad.circleCentre())).colour);
    startTimerHz(60); // the meters' snap needs it
}

void JungleEditor::timerCallback()
{
    pad.refresh();
    updateMute();
    // A new colour fades in over kLogoFade from wherever the last fade got to.
    const auto target = frootLoop(proc.library().entry(pad.cellAt(pad.circleCentre())).colour);
    const double now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    if (target != logoTo)
    {
        logoFrom = logoColour;
        logoTo = target;
        logoFadeStart = now;
    }
    if (logoColour != logoTo)
    {
        const float t = (float) juce::jlimit(0.0, 1.0, (now - logoFadeStart) / kLogoFade);
        logoColour = t >= 1.0f ? logoTo : blendHues(logoFrom, logoTo, t * t * (3.0f - 2.0f * t));
        repaint(statusBar());
    }
}

juce::Colour JungleEditor::blendHues(juce::Colour a, juce::Colour b, float t)
{
    float ha, sa, va, hb, sb, vb;
    a.getHSB(ha, sa, va);
    b.getHSB(hb, sb, vb);
    float dh = hb - ha; // the short way round the wheel
    if (dh > 0.5f) dh -= 1.0f;
    if (dh < -0.5f) dh += 1.0f;
    float h = ha + dh * t;
    h -= std::floor(h);
    return juce::Colour::fromHSV(h, sa + (sb - sa) * t, va + (vb - va) * t, 1.0f);
}

JungleEditor::~JungleEditor()
{
    stopTimer();
    proc.setMuteHeld(false);
}

void JungleEditor::paint(juce::Graphics& g)
{
    const float scale = (float) getWidth() / kDesignSize;
    const auto bar = statusBar().toFloat();
    g.fillAll(cream);
    g.setColour(biscuit); // a shade under the pad, so the logo's white edge shows
    g.fillRect(bar);

    // The logo as outlines, filled with the colour of the break under the
    // circle, sunk into the bar: a white copy sits below it by a quarter of
    // the thickness of the O's bottom, showing as a white edge along the
    // letters' lower sides, as on a pressed-in letter lit from above. Centred
    // across on its ink and up and down on its capitals, the umlauts riding
    // above.
    const auto font = logoFont ? logoFont->withHeight(kLogoHeight * scale)
                               : juce::Font(juce::FontOptions(logoFace).withHeight(kLogoHeight * scale)
                                                .withKerningFactor(kLogoKerning));
    auto outline = [&font](const juce::String& s)
    {
        juce::GlyphArrangement glyphs;
        glyphs.addLineOfText(font, s, 0.0f, 0.0f);
        juce::Path p;
        glyphs.createPath(p);
        return p;
    };
    auto text = outline(logo());
    const auto capitals = outline("L").getBounds(); // a capital's height, no accent
    text.applyTransform(juce::AffineTransform::translation(bar.getCentreX() - text.getBounds().getCentreX(),
                                                           bar.getCentreY() - capitals.getCentreY()));
    // The O's bottom, measured up its middle from its lowest ink to its hole.
    const auto o = outline("O");
    const auto ob = o.getBounds();
    const float x = ob.getCentreX(), step = 0.05f;
    float y = ob.getBottom();
    while (y > ob.getCentreY() && ! o.contains(x, y)) y -= step;  // into the ink
    const float inkBottom = y;
    while (y > ob.getCentreY() && o.contains(x, y)) y -= step;    // through to the hole
    const float bottom = inkBottom - y;
    auto edge = text;
    edge.applyTransform(juce::AffineTransform::translation(0.0f, bottom / 4));
    g.setColour(white);
    g.fillPath(edge);
    g.setColour(logoColour);
    g.fillPath(text);
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
